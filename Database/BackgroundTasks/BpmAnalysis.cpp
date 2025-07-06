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
                // --- Cooperative Startup Delay ---
                // On the very first call, record the start time.
                if (!m_startTime.has_value())
                {
                    m_startTime = std::chrono::steady_clock::now();
                    spdlog::info("BPM Analysis Task: Initializing, will start work in 30s.");
                }

                // Check if 30 seconds have elapsed.
                const auto elapsed = std::chrono::steady_clock::now() - *m_startTime;
                if (elapsed < std::chrono::seconds(30))
                {
                    spdlog::info("BPM Analysis Task: Waiting for startup delay to complete. Elapsed: {} seconds.",
                                std::chrono::duration_cast<std::chrono::seconds>(elapsed).count());
                    // Not time yet. Return immediately, allowing other tasks to run.
                    return;
                }
                // --- End of Startup Delay Logic ---

                // ... The rest of the logic remains the same ...

                std::optional<TrackInfo> trackToProcess = m_trackLibrary.getTrackDatabase()->getNextTrackForBpmAnalysis();
                if (!trackToProcess)
                {
                    spdlog::info("BPM Analysis Task: No more tracks to process. Exiting task.");
                    return; // Nothing to do, return immediately.
                }

                // --- STUBBED IMPLEMENTATION ---
                spdlog::info("BPM Analysis Task: (STUB) Simulating analysis...");
                std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Simulate work
                const int detectedBpm = 120 + (rand() % 10);                 // Generate a fake BPM
                // --- END STUB ---

                spdlog::info("BPM Analysis Task: Detected BPM is {}. Updating database.", detectedBpm);

                // 3. Update the database with the result.
                //m_trackLibrary.updateTrackBpm(trackToProcess->trackId, detectedBpm);
            }
        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio
