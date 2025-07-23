#pragma once

#include <UI/PlaybackController.h>
#include <functional>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
        /**
         * @brief Enhanced audio player component with two-row layout
         *
         * Top row (70%): Transport controls and waveform display
         * Bottom row (30%): Repeat, shuffle, volume, and time displays
         */
        class EnhancedPlayerComponent : public juce::Component, public juce::Timer
        {
        public:
            EnhancedPlayerComponent(PlaybackController &controller);
            ~EnhancedPlayerComponent() override;

            void paint(juce::Graphics &g) override;
            void resized() override;

            // Timer callback for updating UI
            void timerCallback() override;

            // Callbacks for external control
            std::function<void()> onPreviousTrack;
            std::function<void()> onNextTrack;

            // Repeat modes
            enum class RepeatMode
            {
                Off,
                RepeatOne,
                RepeatAll
            };

            RepeatMode getRepeatMode() const
            {
                return m_repeatMode;
            }
            bool isShuffleEnabled() const
            {
                return m_shuffleEnabled;
            }

        private:
            // Top row components
            juce::DrawableButton m_previousButton{"Previous", juce::DrawableButton::ImageFitted};
            juce::DrawableButton m_stopButton{"Stop", juce::DrawableButton::ImageFitted};
            juce::DrawableButton m_playButton{"Play", juce::DrawableButton::ImageFitted};
            juce::DrawableButton m_pauseButton{"Pause", juce::DrawableButton::ImageFitted};
            juce::DrawableButton m_nextButton{"Next", juce::DrawableButton::ImageFitted};

            // Placeholder for waveform (Phase 2)
            juce::Component m_waveformPlaceholder;

            // Bottom row components
            juce::TextButton m_repeatButton{juce::CharPointer_UTF8("\u27F2")};
            juce::ToggleButton m_shuffleButton;
            juce::Label m_speakerIcon;
            juce::Slider m_volumeSlider;
            juce::Label m_currentTimeLabel;
            juce::Label m_totalTimeLabel;

            // Internal state
            PlaybackController &m_playbackController;
            RepeatMode m_repeatMode{RepeatMode::Off};
            bool m_shuffleEnabled{false};

            // Helper methods
            void updateTransportButtons();
            void updateTimeDisplays();
            void updateRepeatButton();
            void updateShuffleButton();
            void updateVolumeIcon(float gain);
            void loadButtonIcons();
            void setupButtons();
            void setupVolumeControl();

            // Button callbacks
            void playButtonClicked();
            void pauseButtonClicked();
            void stopButtonClicked();
            void previousButtonClicked();
            void nextButtonClicked();
            void repeatButtonClicked();
            void shuffleButtonToggled();

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnhancedPlayerComponent)
        };
    } // namespace ui
} // namespace jucyaudio