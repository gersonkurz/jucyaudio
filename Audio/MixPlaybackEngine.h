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

#if MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE

namespace jucyaudio
{
    namespace audio
    {
        using namespace database;

        // Structure to hold information about an active track during playback
        struct PlaybackTrackSource
        {
            TrackId trackId;
            const TrackInfo *trackInfo;
            const MixTrack *mixTrack;

            std::unique_ptr<juce::AudioFormatReader> reader;
            std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
            std::unique_ptr<juce::ResamplingAudioSource> resampler;

            // Current playback position in samples (in source file's sample rate)
            std::atomic<juce::int64> currentPositionInSourceSamples{0};

            PlaybackTrackSource(TrackId id, const TrackInfo *ti, const MixTrack *mt);
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
                return m_totalDurationMs;
            }

            // Check if a mix is loaded
            bool isMixLoaded() const
            {
                return m_mixLoader != nullptr;
            }
            
            // Pause/resume playback
            void setPaused(bool shouldPause) { m_isPaused = shouldPause; }
            
            // Get the current mix loader
            MixProjectLoader* getMixLoader() const
            {
                return m_mixLoader;
            }

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
            std::vector<std::unique_ptr<PlaybackTrackSource>> m_trackSources;

            // Playback state
            std::atomic<juce::int64> m_currentPositionSamples{0};
            std::atomic<bool> m_isPrepared{false};
            std::atomic<bool> m_isPaused{false};
            Duration_t m_totalDurationMs{0};

            // Audio format management
            juce::AudioFormatManager m_formatManager;

            // Playback parameters
            double m_sampleRate{44100.0};
            int m_blockSize{512};

            // Thread safety
            mutable std::mutex m_mutex;

            // Helper methods
            bool prepareTrackSources();
            void mixActiveTracksForBlock(juce::AudioBuffer<float> &buffer, juce::int64 startSample, int numSamples);
            float getEnvelopeGainForTrack(const MixTrack &mixTrack, Duration_t timeInTrack);
            void unloadMixInternal(); // Internal version that doesn't lock

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixPlaybackEngine)
        };

    } // namespace audio
} // namespace jucyaudio

#endif // MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE