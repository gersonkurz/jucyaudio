#include <Database/BackgroundTasks/AudioAnalysis.h>
#include <Database/BackgroundTasks/BpmAnalysis.h>
#include <Database/TrackLibrary.h>
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

                // --- 1. Get a track to process ---
                std::optional<TrackInfo> trackOpt = theTrackLibrary.getTrackDatabase().getNextTrackForBpmAnalysis();
                if (!trackOpt)
                {
                    spdlog::info("BPM Analysis Task: No tracks available for analysis.");
                    return; // No work to do
                }

                const auto &trackInfo = *trackOpt;

                const auto start_time = std::chrono::high_resolution_clock::now();
                const auto trackPath{trackInfo.reconstructFullPath()};
                AudioMetadata am = analyzeAudioFile(trackPath);
                const auto end_time = std::chrono::high_resolution_clock::now();
                const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                // Check if analysis failed (BPM = 0 indicates failure)
                if (am.bpm <= 0.0)
                {
                    spdlog::error("Failed to analyze '{}' - marking as bad format", trackPath.filename().string());

                    // Mark the track as bad format so it won't be retried
                    theTrackLibrary.getTrackDatabase().updateTrackStatus(trackInfo.trackId, TrackStatus::BadFormat);
                }
                else
                {
                    spdlog::info("Analyzed '{}' in {} ms. BPM: {:.2f}, Has Intro: {}, Has Outro: {}",
                        trackPath.filename().string(),
                        duration.count(),
                        am.bpm,
                        am.hasIntro,
                        am.hasOutro);

                    theTrackLibrary.getTrackDatabase().updateTrackBpm(trackInfo.trackId, am);
                }
            }
        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio
