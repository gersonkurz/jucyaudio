#pragma once

#include <Database/Includes/ILongRunningTask.h>
#include <Database/Includes/TrackInfo.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>
#include <memory>
#include <filesystem>

namespace jucyaudio
{
    namespace database
    {
        namespace background_tasks
        {
            /**
             * @brief Task for loading waveforms for a mix with progress reporting
             * 
             * This task loads waveforms for tracks in a mix, either from cache or by
             * generating them from audio files. It provides progress feedback and can
             * be cancelled by the user.
             */
            class WaveformLoadingTask final : public ILongRunningTask
            {
            public:
                /**
                 * @brief Information about a waveform that needs to be loaded
                 */
                struct WaveformRequest
                {
                    TrackId trackId;
                    std::filesystem::path filePath;
                    bool needsLoading;  // false if already cached
                    std::string trackName; // For progress reporting
                };

                /**
                 * @brief Construct a waveform loading task
                 * 
                 * @param requests List of waveforms to load
                 * @param formatManager Audio format manager for loading files
                 * @param thumbnailCache Thumbnail cache for waveform generation
                 */
                explicit WaveformLoadingTask(
                    std::vector<WaveformRequest> requests,
                    juce::AudioFormatManager& formatManager,
                    juce::AudioThumbnailCache& thumbnailCache);
                
                ~WaveformLoadingTask() override = default;

                /**
                 * @brief Get the number of waveforms that were successfully loaded
                 * @return Number of successful loads
                 */
                int getSuccessCount() const { return m_successCount; }
                
                /**
                 * @brief Get the number of waveforms that were already cached
                 * @return Number of cached waveforms
                 */
                int getCachedCount() const { return m_cachedCount; }
                
                /**
                 * @brief Get the tracks that failed to load
                 * @return Vector of track IDs that failed
                 */
                const std::vector<TrackId>& getFailedTracks() const { return m_failedTracks; }

            private:
                /**
                 * @brief Main task execution
                 * 
                 * Processes waveforms sequentially, checking cache first,
                 * then loading from file if needed.
                 */
                void run(ProgressCallback progressCb, 
                         CompletionCallback completionCb, 
                         std::atomic<bool>& shouldCancel) override;

                /**
                 * @brief Load a single waveform from file
                 * 
                 * @param request The waveform request to process
                 * @param progressCb Progress callback for status updates
                 * @return true if successful, false if failed
                 */
                bool loadWaveformFromFile(const WaveformRequest& request,
                                         ProgressCallback progressCb);

                /**
                 * @brief Generate and cache a waveform
                 * 
                 * @param trackId Track ID for caching
                 * @param audioFile File to load
                 * @param progressCb Progress callback for status updates
                 * @return true if successful, false if failed
                 */
                bool generateAndCacheWaveform(TrackId trackId,
                                             const juce::File& audioFile,
                                             ProgressCallback progressCb);

                std::vector<WaveformRequest> m_requests;
                juce::AudioFormatManager& m_formatManager;
                juce::AudioThumbnailCache& m_thumbnailCache;
                
                int m_successCount{0};
                int m_cachedCount{0};
                std::vector<TrackId> m_failedTracks;
                
                // Batch processing settings
                static constexpr int BATCH_SIZE = 3;  // Process 3 waveforms at a time
                static constexpr int LOAD_TIMEOUT_MS = 30000;  // 30 seconds per waveform
            };
        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio