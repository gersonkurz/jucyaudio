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
        // First, identify which tracks need analysis
        std::vector<const database::TrackInfo*> tracksToAnalyze;
        for (const auto& track : m_tracks)
        {
            if (!EnergyAnalyzer::hasValidCachedData(track))
            {
                tracksToAnalyze.push_back(&track);
            }
        }

        if (tracksToAnalyze.empty())
        {
            spdlog::info("EnergyAnalysisTask: All {} tracks already have cached energy data", m_tracks.size());
            progressCb(100, "All tracks already analyzed");
            m_wasSuccessful = true;
            completionCb(true, "Ready to create mix");
            return;
        }

        spdlog::info("EnergyAnalysisTask: Need to analyze {} of {} tracks",
                     tracksToAnalyze.size(), m_tracks.size());

        m_tracksAnalyzedCount = 0;
        const int totalToAnalyze = static_cast<int>(tracksToAnalyze.size());

        for (size_t i = 0; i < tracksToAnalyze.size(); ++i)
        {
            if (shouldCancel.load())
            {
                spdlog::info("EnergyAnalysisTask: Cancelled by user after {} tracks", m_tracksAnalyzedCount);
                m_wasSuccessful = false;
                completionCb(false, "Analysis cancelled");
                return;
            }

            const auto& trackInfo = *tracksToAnalyze[i];
            const int progressPercent = static_cast<int>((i * 100) / totalToAnalyze);

            progressCb(progressPercent, "Analyzing: " + trackInfo.filename);

            // Analyze the track
            const auto filepath = trackInfo.reconstructFullPath();
            auto result = EnergyAnalyzer::analyzeFile(filepath);

            if (result.isValid)
            {
                // Store the analysis results in the database
                const auto jsonStr = result.toJson().dump();
                auto dbResult = theTrackLibrary.getTrackDatabase().updateTrackEnergyData(
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

        progressCb(100, "Analysis complete");

        const std::string resultMsg = "Analyzed " + std::to_string(m_tracksAnalyzedCount) +
                                      " of " + std::to_string(totalToAnalyze) + " tracks";
        spdlog::info("EnergyAnalysisTask: {}", resultMsg);
        m_wasSuccessful = true;
        completionCb(true, resultMsg);
    }

} // namespace jucyaudio::database::background_tasks
