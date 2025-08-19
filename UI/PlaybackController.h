#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <Audio/Equalizer.h>
#include <Audio/Reverb.h>
#include <spdlog/spdlog.h> // For logging within the controller
#include <memory>

namespace jucyaudio
{
    namespace audio 
    {
        class MixPlaybackEngine;
        class MixProjectLoader;
    }
    
    namespace ui
    {
        class PlaybackController final : public juce::ChangeBroadcaster
        {
        public:
            // The 7 states from player-ears.md
            enum class PlayerState
            {
                Silence,              // Nothing loaded, nothing playing
                SilenceTrackLoaded,   // Track loaded but not playing
                TrackPlaying,         // Single track is playing
                TrackPaused,          // Single track is paused (position preserved)
                SilenceMixLoaded,     // Mix loaded but not playing
                MixPlaying,           // Mix is playing
                MixPaused             // Mix is paused (position preserved)
            };

            PlaybackController();
            ~PlaybackController();

            // --- AudioDeviceIOCallback methods to be called by MainComponent ---
            void prepareToPlay(int samplesPerBlockExpected, double sampleRate);
            void getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill);
            void releaseResources();
            bool loadAndPlayFileFromPosition(const juce::File &audioFile, double startPositionSeconds);

            // --- Playback Control Methods ---
            // Returns true if loading was successful and playback started/is starting.
            bool loadAndPlayFile(const juce::File &audioFile);
            
            // Load and play a mix
            bool loadMix(audio::MixProjectLoader* mixLoader);
            void playMixFrom(double absoluteTimelineSeconds);
            
            // State query
            PlayerState getState() const { return m_currentState; }

            // Main control methods - behavior depends on current state
            void play();    // Resume from pause or start from beginning
            void pause();   // Pause and preserve position
            void stop();    // Stop and reset position to 0

            void seek(double positionSeconds);  // Seek within current track/mix
            void setGain(float newGain);
            
            // Playback modes
            void setRepeatMode(bool enabled);
            bool getRepeatMode() const { return m_repeatMode; }
            void setShuffleMode(bool enabled);
            bool getShuffleMode() const { return m_shuffleMode; }

            // --- State Query Methods ---
            bool isPlaying() const;
            bool isMixMode() const;  // True if in any mix state
            double getCurrentPositionSeconds() const;  // Absolute timeline position
            double getLengthInSeconds() const;
            juce::String getCurrentFilepath() const { return m_currentFile.getFullPathName(); }

            // UI State getters for any UI component to query
            bool canPlay() const;
            bool canStop() const;
            bool canPause() const;
            bool isEffectivelyPlaying() const;
            
            // Callback for state changes
            std::function<void(PlayerState)> onStateChanged;

            // --- Access to TransportSource for MainComponent to be a ChangeListener ---
            // This allows MainComponent to listen for when the transport source itself stops (e.g., end of file)
            juce::AudioTransportSource &getTransportSource()
            {
                return m_audioTransportSource;
            }

            // VU Meter processing
            void processAudioBlock(const juce::AudioBuffer<float>& buffer);
            float getPeakLeft() const { return m_peakLeft.load(); }
            float getPeakRight() const { return m_peakRight.load(); }
            
            // Master EQ Control
            void updateMasterEQ(const audio::model::EQSettings& settings);
            
            // Master Reverb Control
            void updateMasterReverb(const audio::model::ReverbSettings& settings);

            // Thread-safe execution for mix-related operations
            void withMixEngineLock(std::function<void()> action);

        private:
            void changeState(PlayerState newState);
            void loadFileInternal(const juce::File &audioFile);
            void unloadAudioSource();
            void unloadMix();

            // Audio components
            juce::AudioFormatManager m_audioFormatManager;
            std::unique_ptr<juce::AudioFormatReaderSource> m_currentAudioFileSource;
            juce::AudioTransportSource m_audioTransportSource;
            audio::Equalizer m_masterEqualizer;
            audio::Reverb m_masterReverb;
            
            // Mix playback support
            std::unique_ptr<audio::MixPlaybackEngine> m_mixPlaybackEngine;
            audio::MixProjectLoader* m_currentMixLoader{nullptr};
            
            // Device state
            double m_deviceSampleRate{0.0};
            int m_deviceBlockSize{0};
            bool m_isDevicePrepared{false};
            
            // Current state
            PlayerState m_currentState{PlayerState::Silence};
            juce::File m_currentFile;
            
            // Position tracking (absolute timeline position in seconds)
            double m_pausedPosition{0.0};

            // VU Meter atomics
            std::atomic<float> m_peakLeft{0.0f};
            std::atomic<float> m_peakRight{0.0f};

            // To prevent re-entrancy or rapid state changes from UI/callbacks
            std::atomic<bool> m_isCurrentlyChangingState{false};
            
            // Playback modes
            bool m_repeatMode{false};
            bool m_shuffleMode{false};
        };
    } // namespace ui
} // namespace jucyaudio