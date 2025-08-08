#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
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
        class PlaybackController final : public juce::ChangeBroadcaster // So MainComponent can listen to general state changes if needed
        {
        public:
            enum class State
            {
                Stopped,
                Starting, // Transient: loading/preparing
                Playing,
                Pausing, // Transient: about to pause
                Paused,
                Stopping // Transient: about to stop
            };
            
            enum class PlaybackMode
            {
                SingleTrack,
                MixPreview
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
            
            // Load and play a mix preview
            bool loadAndPlayMix(audio::MixProjectLoader* mixLoader, double startPositionSeconds = 0.0);
            
            // Switch between single track and mix preview modes
            void setPlaybackMode(PlaybackMode mode);
            PlaybackMode getPlaybackMode() const { return m_playbackMode; }

            void play(); // Plays if a file is loaded and paused/stopped, or resumes.
            void pause();
            void stop(); // Stops playback and potentially unloads the source.
            void stopSingleTrackOnly(); // Stops only single track playback without affecting mix
            void togglePlayPause();

            void seek(double positionSeconds);
            void setGain(float newGain); // 0.0 to 1.0 (or higher)

            // --- State Query Methods ---
            bool isPlaying() const;
            double getCurrentPositionSeconds() const;
            double getLengthInSeconds() const;
            State getCurrentState() const
            {
                return m_currentState;
            }
            juce::String getCurrentFilepath() const
            {
                return m_currentFile.getFullPathName();
            }

            // UI State getters for any UI component to query
            bool canPlay() const;
            bool canStop() const;
            bool canPause() const;
            bool isEffectivelyPlaying() const;
            
            // Callback to stop mix playback when stop/pause is pressed
            std::function<void()> onStopMixPlayback;
            
            // Callback to check if mix is currently playing
            std::function<bool()> isMixPlaying;

            // --- Access to TransportSource for MainComponent to be a ChangeListener ---
            // This allows MainComponent to listen for when the transport source itself stops (e.g., end of file)
            juce::AudioTransportSource &getTransportSource()
            {
                return m_audioTransportSource;
            }

        private:
            void changeState(State newState);
            void loadFileInternal(const juce::File &audioFile);
            void unloadAudioSource();

            juce::AudioFormatManager m_audioFormatManager;
            std::unique_ptr<juce::AudioFormatReaderSource> m_currentAudioFileSource;
            juce::AudioTransportSource m_audioTransportSource;
            
            // Mix playback support
            std::unique_ptr<audio::MixPlaybackEngine> m_mixPlaybackEngine;
            PlaybackMode m_playbackMode{PlaybackMode::SingleTrack};
            
            double m_deviceSampleRate{0.0};
            int m_deviceBlockSize{0};
            bool m_isDevicePrepared{false};
            juce::File m_currentFile; // Keep track of the currently loaded file
            State m_currentState{State::Stopped};

            // To prevent re-entrancy or rapid state changes from UI/callbacks
            std::atomic<bool> m_isCurrentlyChangingState{false};
        };
    } // namespace ui
} // namespace jucyaudio