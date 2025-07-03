#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <spdlog/spdlog.h> // For logging within the controller

namespace jucyaudio
{
    namespace ui
    {
        class PlaybackToolbarComponent;

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

            PlaybackController(PlaybackToolbarComponent &toolbar);
            ~PlaybackController();

            // --- AudioDeviceIOCallback methods to be called by MainComponent ---
            void prepareToPlay(int samplesPerBlockExpected, double sampleRate);
            void getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill);
            void releaseResources();

            // --- Playback Control Methods ---
            // Returns true if loading was successful and playback started/is starting.
            bool loadAndPlayFile(const juce::File &audioFile);

            void play(); // Plays if a file is loaded and paused/stopped, or resumes.
            void pause();
            void stop(); // Stops playback and potentially unloads the source.
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

            void onTimerEvent(); // Timer callback to handle state changes and updates
            void syncUIToPlaybackControllerState(bool hasRowSelected);

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
            double m_deviceSampleRate{0.0};
            int m_deviceBlockSize{0};
            bool m_isDevicePrepared{false};
            juce::File m_currentFile; // Keep track of the currently loaded file
            State m_currentState{State::Stopped};
            PlaybackToolbarComponent &m_playbackToolbar;

            // To prevent re-entrancy or rapid state changes from UI/callbacks
            std::atomic<bool> m_isCurrentlyChangingState{false};
        };
    } // namespace ui
} // namespace jucyaudio