#include <Database/BackgroundTasks/BpmAnalysis.h>
#include <Database/BackgroundTasks/BpmAnalysisTask.h>
#include <Database/TrackLibrary.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        namespace background_tasks
        {
            void BpmAnalysis::processWork()
            {
                spdlog::info("BPM Analysis Task: Starting work...");
                // --- Cooperative Startup Delay (unchanged, correct) ---
                if (!m_startTime.has_value())
                {
                    m_startTime = std::chrono::steady_clock::now();
                }
                if (std::chrono::steady_clock::now() - *m_startTime < std::chrono::seconds(5))
                {
                    spdlog::info("BPM Analysis Task: Waiting for 5 seconds before starting work.");
                    return;
                }

                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                analyzeNextTrack(formats);
            }

            bool analyzeNextTrack(juce::AudioFormatManager &formats)
            {
                auto &trackDb = theTrackLibrary.getTrackDatabase();
                const auto trackOpt = trackDb.getNextTrackForBpmAnalysis();
                if (!trackOpt)
                {
                    spdlog::info("BPM Analysis Task: No tracks available for analysis.");
                    return false;
                }
                const auto &trackInfo = *trackOpt;

                const auto startTime = std::chrono::steady_clock::now();
                TrackAnalysis outcome;
                std::filesystem::path trackPath;
                try
                {
                    trackPath = trackInfo.reconstructFullPath();
                    outcome = analyzeTrack(formats, trackPath);
                }
                catch (const std::exception &e)
                {
                    outcome = TrackAnalysis{std::nullopt, SkipReason::ReadError, e.what()};
                }
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime);

                if (outcome.metadata.has_value())
                {
                    // A BPM of 0 is a result, not a failure: the file decoded and has no beat to find -
                    // ambient, drones, spoken word. Stored as 0, which the selection counts as analyzed,
                    // and the track is ok. It used to be marked bad format, the only way to stop the
                    // selection picking it again (#78).
                    spdlog::info("Analyzed '{}' in {} ms. BPM: {:.2f}", trackPath.filename().string(), elapsed.count(), outcome.metadata->bpm);
                    trackDb.updateTrackBpm(trackInfo.trackId, *outcome.metadata);
                }
                else
                {
                    // Not decoded, or no file: marked so the selection does not pick it again.
                    spdlog::error("Failed to analyze '{}' ({}{}{}) - marking as bad format",
                        trackPath.filename().string(),
                        describe(outcome.reason),
                        outcome.detail.empty() ? "" : ": ",
                        outcome.detail);
                    trackDb.updateTrackStatus(trackInfo.trackId, TrackStatus::BadFormat);
                }
                return true;
            }
        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio
