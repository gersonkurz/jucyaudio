#pragma once

#include <Database/Includes/ILongRunningTask.h>
#include <Database/Includes/TrackInfo.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>
#include <memory>
#include <filesystem>
#include <optional>

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
                    TrackId trackId{0};
                    std::filesystem::path filePath;
                    bool needsLoading{true}; // false if already cached
                    std::string trackName; // For progress reporting
                };

                /**
                 * @brief Why a waveform could not be produced.
                 *
                 * Only some of these say anything about the audio itself. A missing file usually means a
                 * temporarily offline drive, a timeout means the machine was busy, and a cache write
                 * failure happens *after* the audio decoded perfectly well. Callers that act on failures -
                 * by dropping the track from a mix, say - must distinguish these from a real decode
                 * failure; see provesAudioUnusable().
                 */
                enum class FailureKind
                {
                    FileMissing,      ///< The path did not resolve.
                    SourceOpenFailed, ///< The file would not open for reading; nothing was decoded.
                    Timeout,          ///< The decoder did not finish in time.
                    CacheWriteFailed, ///< Decoded fine; only storing the waveform failed.
                    EmptyWaveform,    ///< Decoded, but to nothing at all.
                    DecodeFailed      ///< The decoder rejected the content.
                };

                struct FailedWaveform
                {
                    TrackId trackId{0};
                    std::string trackName;
                    std::filesystem::path filePath;
                    std::string reason;
                    FailureKind kind{FailureKind::DecodeFailed};
                };

                /// @brief Whether a failure proves the audio itself cannot be decoded.
                /// @param kind The failure category.
                /// @return true only for failures caused by the content, not by its surroundings.
                static bool provesAudioUnusable(FailureKind kind)
                {
                    // SourceOpenFailed is deliberately absent. JUCE reports it for anything that stops
                    // it establishing a sample rate and length - a permission error, a disconnected
                    // volume, a file deleted between the existence check and the open - none of which
                    // is a statement about the audio. Treating it as one would let a transient failure
                    // drop a track out of the mix.
                    return kind == FailureKind::EmptyWaveform || kind == FailureKind::DecodeFailed;
                }

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
                 * @return Vector of failure details
                 */
                const std::vector<FailedWaveform>& getFailedTracks() const { return m_failedTracks; }

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
                 * @return Empty on success, otherwise a human-readable failure reason
                 */
                /// @brief A failure category paired with its human-readable explanation.
                struct FailureDetail
                {
                    FailureKind kind;
                    std::string reason;
                };

                std::optional<FailureDetail> loadWaveformFromFile(const WaveformRequest& request,
                                                                  ProgressCallback progressCb);

                /**
                 * @brief Generate and cache a waveform
                 * 
                 * @param trackId Track ID for caching
                 * @param audioFile File to load
                 * @param progressCb Progress callback for status updates
                 * @return Empty on success, otherwise a human-readable failure reason
                 */
                std::optional<FailureDetail> generateAndCacheWaveform(TrackId trackId,
                                                                      const juce::File& audioFile,
                                                                      ProgressCallback progressCb);

                std::string buildFinalMessage(int needLoadingCount) const;
                std::string buildFailureLabel(const FailedWaveform& failure) const;

                std::vector<WaveformRequest> m_requests;
                juce::AudioFormatManager& m_formatManager;
                juce::AudioThumbnailCache& m_thumbnailCache;
                
                int m_successCount{0};
                int m_cachedCount{0};
                std::vector<FailedWaveform> m_failedTracks;
                
                // Batch processing settings
                static constexpr int BATCH_SIZE = 3;  // Process 3 waveforms at a time
                static constexpr int LOAD_TIMEOUT_MS = 30000;  // 30 seconds per waveform
            };
        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio
