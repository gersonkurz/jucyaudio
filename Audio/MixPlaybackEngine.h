#pragma once

#include <Audio/MixProjectLoader.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/Includes/Constants.h>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <mutex>
#include <vector>

namespace jucyaudio
{
    namespace audio
    {
        using namespace database;

        // Forward declaration
        struct PlaybackTrackSource;

        /**
         * @brief Refcounted container holding all data needed for mix playback.
         *
         * This struct owns copies of TrackInfo data and the PlaybackTrackSource objects.
         *
         * Thread-safety: Managed via std::shared_ptr and std::atomic.
         *
         * Ownership model (Garbage Collection Pattern):
         * - m_currentPlaybackState is a std::atomic<std::shared_ptr<PlaybackState>>.
         * - Audio thread loads a local shared_ptr, ensuring the state stays alive during the callback.
         * - Main thread swaps m_currentPlaybackState with a new state.
         * - The old state is moved to m_garbage list.
         * - Main thread periodically scans m_garbage. If use_count() == 1 (only held by garbage),
         *   it is safe to delete (destructor runs on main thread).
         */
        struct PlaybackState
        {
            std::vector<database::TrackInfo> trackInfos;  // Owned copies from MixProjectLoader
            std::vector<std::unique_ptr<PlaybackTrackSource>> trackSources;
            std::vector<Duration_t> trackStartTimes;
            Duration_t totalDuration{0};

            // Helper to find TrackInfo by ID (returns pointer into trackInfos vector)
            const TrackInfo* getTrackInfo(TrackId trackId) const
            {
                auto it = std::find_if(trackInfos.begin(), trackInfos.end(),
                    [trackId](const auto& ti) { return ti.trackId == trackId; });
                return (it != trackInfos.end()) ? &(*it) : nullptr;
            }
        };

        // Structure to hold information about an active track during playback
        struct PlaybackTrackSource
        {
            TrackId trackId;
            size_t mixTrackIndex;        // Original index into mixTracks/trackStartTimes (for correct timing lookup)
            const TrackInfo *trackInfo;  // SAFE: Points into PlaybackState->trackInfos (owned by PlaybackState)
            MixTrack mixTrack;           // OWNED copy (MixTrack is POD, cheap to copy)

            std::unique_ptr<juce::AudioFormatReader> reader;
            std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
            std::unique_ptr<juce::ResamplingAudioSource> resampler;

            // Current playback position in samples (in source file's sample rate)
            std::atomic<juce::int64> currentPositionInSourceSamples{0};

            // Pre-calculated sample positions at target sample rate (avoids per-block math)
            juce::int64 startSampleAtTargetRate{0};
            juce::int64 endSampleAtTargetRate{0};
            juce::int64 cueStartSamples{0};  // File read offset for cueStart (in SOURCE sample rate)

            PlaybackTrackSource(TrackId id, size_t index, const TrackInfo *ti, const MixTrack& mt);
            ~PlaybackTrackSource() = default;

            bool prepare(juce::AudioFormatManager &formatManager, double targetSampleRate, int blockSize,
                        Duration_t trackStartTime);  // trackStartTime from trackStartTimes array
            juce::AudioSource *getAudioSource()
            {
                return resampler ? static_cast<juce::AudioSource*>(resampler.get())
                                 : static_cast<juce::AudioSource*>(readerSource.get());
            }
        };

        /**
         * MixPlaybackEngine - Real-time playback engine for mix previews
         *
         * This class handles the real-time mixing of multiple tracks with overlaps,
         * crossfades, and envelope-based volume control. It's designed to give an
         * accurate preview of what the exported mix will sound like.
         */
        class MixPlaybackEngine : public juce::AudioIODeviceCallback, public juce::AudioSource
        {
        public:
            MixPlaybackEngine();
            ~MixPlaybackEngine() override;

            // Load a mix for playback
            bool loadMix(MixProjectLoader *mixLoader);

            // Unload the current mix
            void unloadMix();

            // Seek to a specific position in the mix (in milliseconds)
            void setPosition(Duration_t positionMs);

            // Get current playback position (in milliseconds)
            Duration_t getPosition() const;

            // Get total mix duration (in milliseconds)
            Duration_t getTotalDuration() const
            {
                auto state = m_currentPlaybackState.load();
                return state ? state->totalDuration : Duration_t{0};
            }

            // Check if a mix is loaded
            bool isMixLoaded() const
            {
                return m_mixLoader != nullptr;
            }
            
            // Pause/resume playback
            void setPaused(bool shouldPause);  // Moved to implementation for logging
            
            // Volume control
            void setGain(float gain) { m_masterGain = gain; }
            float getGain() const { return m_masterGain; }
            
            // Get the current mix loader
            MixProjectLoader* getMixLoader() const
            {
                return m_mixLoader;
            }

            // Expose the critical section for external locking
            juce::CriticalSection& getLock() { return m_critSec; }
            
            // Recalculate track positions after attach points change
            // This should be called when cue/attach points are modified
            void recalculateTrackPositions();

            // AudioSource interface
            void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
            void releaseResources() override;
            void getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;

            // AudioIODeviceCallback interface
            void audioDeviceIOCallbackWithContext(const float *const *inputChannelData,
                                                  int numInputChannels,
                                                  float *const *outputChannelData,
                                                  int numOutputChannels,
                                                  int numSamples,
                                                  const juce::AudioIODeviceCallbackContext &context) override;
            void audioDeviceAboutToStart(juce::AudioIODevice *device) override;
            void audioDeviceStopped() override;

        private:
            // Mix data
            MixProjectLoader *m_mixLoader{nullptr};

            // Thread-safe state management.
            // Note: std::atomic<std::shared_ptr<T>> is not lock-free on MSVC/x64 (uses spinlock).
            // This means the audio thread may briefly contend with loadMix(). Acceptable tradeoff
            // since loadMix() is infrequent. If glitches occur, consider SeqLock or intrusive refcount.
            std::atomic<std::shared_ptr<PlaybackState>> m_currentPlaybackState{nullptr};

            // Garbage collection for old states.
            // Old states are held here until the audio thread is definitely done with them
            // (checked via use_count() == 1). This ensures destructors run on the main thread.
            // Threading: loadMix()/unloadMix() are expected to run on the UI thread only; if
            // that changes, access to m_garbage must be synchronized.
            std::vector<std::shared_ptr<PlaybackState>> m_garbage;

            // Playback state
            std::atomic<juce::int64> m_currentPositionSamples{0};
            std::atomic<bool> m_isPrepared{false};
            std::atomic<bool> m_isPaused{false};

            // Lock-free seek support
            std::atomic<bool> m_pendingSeek{false};
            std::atomic<int64_t> m_targetPositionMs{0};

            // Audio format management
            juce::AudioFormatManager m_formatManager;

            // Playback parameters
            double m_sampleRate{44100.0};
            int m_blockSize{512};
            std::atomic<float> m_masterGain{1.0f};

            // Pre-allocated scratch buffer for audio callback (avoids allocation in real-time thread)
            juce::AudioBuffer<float> m_scratchBuffer;

            // Thread safety
            mutable juce::CriticalSection m_critSec;

            // Helper methods
            std::shared_ptr<PlaybackState> buildPlaybackState(MixProjectLoader* mixLoader);
            void collectGarbage(); // Sweep m_garbage and delete unused states
            void mixActiveTracksForBlock(const std::shared_ptr<PlaybackState>& state, juce::AudioBuffer<float> &buffer, juce::int64 startSample, int numSamples);
            float getEnvelopeGainForTrack(const MixTrack &mixTrack, Duration_t timeInTrack);
            void unloadMixInternal(); // Internal version that doesn't lock

            void setPositionInternal(Duration_t positionMs); // Internal version that assumes mutex is already locked

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixPlaybackEngine)
        };

    } // namespace audio
} // namespace jucyaudio
