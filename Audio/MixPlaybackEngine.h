#pragma once

#include <Audio/MixProjectLoader.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/Includes/Constants.h>
#include <Database/Includes/IRefCounted.h>
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
         * The audio thread holds a reference to PlaybackState (via atomic pointer), making
         * it safe for PlaybackTrackSource to hold pointers into trackInfos.
         *
         * Thread-safety: The atomic pointer in MixPlaybackEngine ensures only one thread
         * can modify the pointer at a time. The audio thread only reads.
         *
         * Ownership model (Double-Buffer Pattern):
         * - PlaybackState is refcounted (starts with refcount=1)
         * - m_currentPlaybackState atomic pointer holds the active state
         * - m_previousState holds the prior state (from previous swap)
         * - On swap: old m_previousState is deleted, current becomes previous
         * - This guarantees audio thread finishes with old state before deletion
         * - Audio thread reads atomically - no retain/release needed in callback
         */
        struct PlaybackState : public database::RefCountImpl
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

            PlaybackTrackSource(TrackId id, size_t index, const TrackInfo *ti, const MixTrack& mt);
            ~PlaybackTrackSource() = default;

            bool prepare(juce::AudioFormatManager &formatManager, double targetSampleRate, int blockSize);
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
                auto* state = m_currentPlaybackState.load();
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

            // Refcounted playback state (atomic swap for lock-free audio thread)
            std::atomic<PlaybackState*> m_currentPlaybackState{nullptr};

            // Double-buffer pattern: keep previous state alive until next swap
            // This ensures audio thread always has a valid state during transitions
            PlaybackState* m_previousState{nullptr};

            // DEPRECATED: Old members kept temporarily for compatibility with non-audio methods
            // TODO: Remove these after updating all non-audio-callback methods to use PlaybackState
            std::vector<std::unique_ptr<PlaybackTrackSource>> m_trackSources;
            std::vector<Duration_t> m_trackStartTimes;
            Duration_t m_totalDurationMs{0};

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
            PlaybackState* buildPlaybackState(MixProjectLoader* mixLoader);  // Build new PlaybackState from MixProjectLoader
            void retireState(PlaybackState* oldState);  // Double-buffer: retire old state, delete previous
            void mixActiveTracksForBlock(juce::AudioBuffer<float> &buffer, juce::int64 startSample, int numSamples);
            float getEnvelopeGainForTrack(const MixTrack &mixTrack, Duration_t timeInTrack);
            void unloadMixInternal(); // Internal version that doesn't lock

            // DEPRECATED: Old methods kept temporarily for compatibility
            // TODO: Remove these after updating all calling code
            void calculateTrackStartTimes(); // Calculate track positions using Mix Flow algorithm (DEPRECATED - moved to PlaybackState)
            bool prepareTrackSources(); // DEPRECATED - logic moved to buildPlaybackState
            void setPositionInternal(Duration_t positionMs); // Internal version that assumes mutex is already locked

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixPlaybackEngine)
        };

    } // namespace audio
} // namespace jucyaudio
