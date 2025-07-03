#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace jucyaudio
{
    namespace ui
    {
        class PlaybackToolbarComponent : public juce::Component, public juce::Button::Listener, public juce::Slider::Listener
        {
        public:
            PlaybackToolbarComponent();
            ~PlaybackToolbarComponent() override;

            void paint(juce::Graphics &g) override;
            void resized() override;

            // To connect to MainComponent's playback logic
            std::function<void()> onPlayClicked;
            std::function<void()> onPauseClicked; // Or combine with onPlayClicked for a toggle
            std::function<void()> onStopClicked;
            // std::function<void(double newPosition)> onSeek; // For slider

            // To update the toolbar's state (e.g., Play button becomes Pause)
            void setPlayButtonEnabled(bool enabled);
            void setStopButtonEnabled(bool enabled);
            void setIsPlaying(bool isPlaying); // Changes Play button to Pause icon/text

            std::function<void(double newNormalizedPosition)> onPositionSeek; // Position 0.0 to 1.0
            std::function<void(float newGain)> onVolumeChanged;               // Gain 0.0 to 1.0 (or more)

            void updateTimeDisplay(double currentSeconds, double totalSeconds);
            void setPositionSliderRange(double totalSeconds);   // Call when new track loaded
            void setPositionSliderValue(double currentSeconds); // Call from MainComponent timer
            void setVolumeSliderValue(float gain);              // Call from MainComponent to init

            bool isPositionSliderDragging() const
            {
                return m_isPositionSliderBeingDragged;
            }

        private:
            void buttonClicked(juce::Button *button) override;
            void sliderValueChanged(juce::Slider *slider) override;
            void sliderDragEnded(juce::Slider *slider) override;
            void sliderDragStarted(juce::Slider *slider) override;

            juce::DrawableButton m_playButton{"playButton", juce::DrawableButton::ImageOnButtonBackground};
            juce::DrawableButton m_pauseButton{"pauseButton", juce::DrawableButton::ImageOnButtonBackground}; // Separate button for pause
            juce::DrawableButton m_stopButton{"stopButton", juce::DrawableButton::ImageOnButtonBackground};

            void createIconDrawables(); // Helper to create icon paths

            std::unique_ptr<juce::Drawable> m_playDrawable;
            std::unique_ptr<juce::Drawable> m_pauseDrawable;
            std::unique_ptr<juce::Drawable> m_stopDrawable;

            juce::Slider m_positionSlider{juce::Slider::LinearHorizontal, juce::Slider::TextBoxLeft};
            juce::Label m_currentTimeLabel{{}, "0:00"};
            juce::Label m_totalTimeLabel{{}, "0:00"};
            juce::Slider m_volumeSlider{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
            bool m_isPositionSliderBeingDragged{false};

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaybackToolbarComponent)
        };

    } // namespace ui
} // namespace jucyaudio