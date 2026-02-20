/*
 * This file is part of jucyaudio.
 * Copyright (C) 2025 Gerson Kurz <not@p-nand-q.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <Database/BackgroundTasks/EnergyAnalysisTask.h>
#include <Database/BackgroundTasks/EnergyAnalyzer.h>
#include <Database/TrackLibrary.h>
#include <spdlog/spdlog.h>

namespace jucyaudio::database::background_tasks
{
    EnergyAnalysisTask::EnergyAnalysisTask(const std::vector<database::TrackInfo>& tracks)
        : ILongRunningTask{"Analyzing tracks for smart transitions", true},
          m_tracks{tracks}
    {
    }

    void EnergyAnalysisTask::run(database::ProgressCallback progressCb,
                                  database::CompletionCallback completionCb,
                                  std::atomic<bool>& shouldCancel)
    {
        struct WorkItem
        {
            const database::TrackInfo* track{nullptr};
            bool needsEnergy{false};
            bool needsWaveform{false};
        };

        // First, identify which tracks need processing
        std::vector<WorkItem> tracksToProcess;
        auto& db = theTrackLibrary;
        for (const auto& track : m_tracks)
        {
            const bool needsEnergy = !EnergyAnalyzer::hasValidCachedData(track);

            std::vector<unsigned char> cachedWaveform;
            const bool hasWaveform = db.loadWaveform(track.trackId, cachedWaveform).isOk() && !cachedWaveform.empty();
            const bool needsWaveform = !hasWaveform;

            if (needsEnergy || needsWaveform)
            {
                tracksToProcess.push_back(WorkItem{&track, needsEnergy, needsWaveform});
            }
        }

        if (tracksToProcess.empty())
        {
            spdlog::info("EnergyAnalysisTask: All {} tracks already have cached energy + waveform data", m_tracks.size());
            progressCb(100, "All tracks already processed");
            m_wasSuccessful = true;
            completionCb(true, "Ready to create mix");
            return;
        }

        spdlog::info("EnergyAnalysisTask: Need to process {} of {} tracks",
                     tracksToProcess.size(), m_tracks.size());

        m_tracksAnalyzedCount = 0;
        int waveformsCachedCount = 0;
        const int totalToProcess = static_cast<int>(tracksToProcess.size());

        for (size_t i = 0; i < tracksToProcess.size(); ++i)
        {
            if (shouldCancel.load())
            {
                spdlog::info("EnergyAnalysisTask: Cancelled by user after {} tracks", m_tracksAnalyzedCount);
                m_wasSuccessful = false;
                completionCb(false, "Analysis cancelled");
                return;
            }

            const auto& item = tracksToProcess[i];
            const auto& trackInfo = *item.track;
            const int progressPercent = static_cast<int>((i * 100) / totalToProcess);

            progressCb(progressPercent, "Processing: " + trackInfo.filename);

            // Decode once, then compute energy + waveform in parallel branches.
            const auto filepath = trackInfo.reconstructFullPath();
            std::vector<unsigned char> waveformBlob;
            auto result = EnergyAnalyzer::analyzeFile(filepath, item.needsWaveform ? &waveformBlob : nullptr);

            if (item.needsEnergy)
            {
                if (result.isValid)
                {
                    // Store the analysis results in the database
                    const auto jsonStr = result.toJson().dump();
                    auto dbResult = db.getTrackDatabase().updateTrackEnergyData(
                        trackInfo.trackId, result.introEnd, result.outroStart, jsonStr);

                    if (dbResult.isOk())
                    {
                        spdlog::debug("EnergyAnalysisTask: Stored energy data for track {} (intro={}ms, outro={}ms)",
                                      trackInfo.trackId, result.introEnd.count(), result.outroStart.count());
                        ++m_tracksAnalyzedCount;
                    }
                    else
                    {
                        spdlog::warn("EnergyAnalysisTask: Failed to store energy data for track {}: {}",
                                     trackInfo.trackId, dbResult.errorMessage);
                    }
                }
                else
                {
                    spdlog::warn("EnergyAnalysisTask: Failed to analyze track {} ({})",
                                 trackInfo.trackId, trackInfo.filename);
                    // Continue with other tracks - mix creation will use fallback for this one
                }
            }

            if (item.needsWaveform)
            {
                if (!waveformBlob.empty())
                {
                    auto waveformSaveResult = db.saveWaveform(trackInfo.trackId, waveformBlob);
                    if (waveformSaveResult.isOk())
                    {
                        ++waveformsCachedCount;
                        spdlog::debug("EnergyAnalysisTask: Cached waveform for track {} ({} bytes)",
                                      trackInfo.trackId, waveformBlob.size());
                    }
                    else
                    {
                        spdlog::warn("EnergyAnalysisTask: Failed to cache waveform for track {}: {}",
                                     trackInfo.trackId, waveformSaveResult.errorMessage);
                    }
                }
                else
                {
                    spdlog::warn("EnergyAnalysisTask: Waveform blob generation failed for track {} ({})",
                                 trackInfo.trackId, trackInfo.filename);
                }
            }
        }

        progressCb(100, "Processing complete");

        const std::string resultMsg =
            "Analyzed " + std::to_string(m_tracksAnalyzedCount) + " tracks, cached " +
            std::to_string(waveformsCachedCount) + " waveforms";
        spdlog::info("EnergyAnalysisTask: {}", resultMsg);
        m_wasSuccessful = true;
        completionCb(true, resultMsg);
    }

} // namespace jucyaudio::database::background_tasks
