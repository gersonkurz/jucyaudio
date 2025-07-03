#include <UI/PlaybackToolbarComponent.h>
#include <spdlog/spdlog.h>
#include "BinaryData.h"

namespace jucyaudio
{
    namespace ui
    {
        std::unique_ptr<juce::Drawable> loadSvgIcon(const char *resourceName)
        {
            int dataSize = 0;
            const char *data = BinaryData::getNamedResource(resourceName, dataSize);
            if (data != nullptr && dataSize > 0)
            {
                std::unique_ptr<juce::Drawable> drawable = juce::Drawable::createFromImageData(data, static_cast<size_t>(dataSize));
                if (drawable)
                {
                    // Optionally set a default fill color if the SVG doesn't define one or you want to override
                    // This works best if the SVG is a simple path.
                    // if (auto* dp = dynamic_cast<juce::DrawablePath*>(drawable.get())) {
                    //     dp->setFill(findColour(juce::DrawableButton::textColourId));
                    // } else if (auto* dc = dynamic_cast<juce::DrawableComposite*>(drawable.get())) {
                    //     // For composites, you might need to iterate children or ensure SVGs are single color
                    // }
                    return drawable;
                }
                else
                {
                    spdlog::error("Failed to create drawable from SVG resource: {}", resourceName);
                }
            }
            else
            {
                spdlog::error("SVG resource not found in BinaryData: {}", resourceName);
            }
            return nullptr;
        }

        void PlaybackToolbarComponent::createIconDrawables()
        {
            // Use the C++ identifiers for your resources from BinaryData.h
            // These are examples, replace with your actual resource names
            m_playDrawable = loadSvgIcon("play_arrow_svg");
            m_pauseDrawable = loadSvgIcon("pause_svg");
            m_stopDrawable = loadSvgIcon("stop_svg");

            // Optional: If SVGs don't have a fill or you want to unify their color:
            juce::Colour iconTint = findColour(juce::DrawableButton::textColourId);
            if (m_playDrawable)
                m_playDrawable->replaceColour(juce::Colours::black, iconTint); // If SVGs are black
            if (m_pauseDrawable)
                m_pauseDrawable->replaceColour(juce::Colours::black, iconTint);
            if (m_stopDrawable)
                m_stopDrawable->replaceColour(juce::Colours::black, iconTint);
            // replaceColour is quite effective for single-color SVGs or SVGs where you know the original color.
        }

        PlaybackToolbarComponent::PlaybackToolbarComponent()
        {
            createIconDrawables();

            addAndMakeVisible(m_playButton);
            m_playButton.setImages(m_playDrawable.get()); // Only normal image
            m_playButton.addListener(this);
            m_playButton.setTooltip("Play");

            addAndMakeVisible(m_pauseButton);
            m_pauseButton.setImages(m_pauseDrawable.get());
            m_pauseButton.addListener(this);
            m_playButton.setTooltip("Pause");
            // m_pauseButton.setVisible(false); // Initially hide pause button, controlled by setIsPlaying

            addAndMakeVisible(m_stopButton);
            m_stopButton.setImages(m_stopDrawable.get());
            m_stopButton.addListener(this);
            m_playButton.setTooltip("Stop");

            // Initial state: show Play, hide Pause
            setIsPlaying(false); // This will set button visibility

            addAndMakeVisible(m_currentTimeLabel);
            m_currentTimeLabel.setJustificationType(juce::Justification::centredRight);
            m_currentTimeLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(12.0f)});

            addAndMakeVisible(m_positionSlider);
            m_positionSlider.setRange(0.0, 1.0, 0.001);                             // Normalized range initially
            m_positionSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); // No direct text box initially
            m_positionSlider.addListener(this);

            addAndMakeVisible(m_totalTimeLabel);
            m_totalTimeLabel.setJustificationType(juce::Justification::centredLeft);
            m_totalTimeLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(12.0f)});

            addAndMakeVisible(m_volumeSlider);
            m_volumeSlider.setRange(0.0, 1.0, 0.01);                   // Volume 0.0 to 1.0
            m_volumeSlider.setValue(0.75, juce::dontSendNotification); // Default volume
            m_volumeSlider.addListener(this);
            m_volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            m_volumeSlider.setSkewFactorFromMidPoint(0.3); // Logarithmic feel for volume
        }

        PlaybackToolbarComponent::~PlaybackToolbarComponent()
        {
            m_stopButton.removeListener(this);
            m_positionSlider.removeListener(this);
            m_volumeSlider.removeListener(this);
        }

        void PlaybackToolbarComponent::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId).darker(0.1f));
            g.setColour(juce::Colours::darkgrey);
            g.drawRect(getLocalBounds(), 1); // Simple border
        }

        void PlaybackToolbarComponent::resized()
        {
            auto bounds = getLocalBounds().reduced(5);
            int buttonWidth = 30;
            int timeLabelWidth = 45;
            int volumeSliderWidth = 80;

            m_playButton.setBounds(bounds.removeFromLeft(buttonWidth)); // Play/Pause will occupy this slot
            m_pauseButton.setBounds(m_playButton.getBounds());          // Place Pause button in the same spot

            bounds.removeFromLeft(5);
            m_stopButton.setBounds(bounds.removeFromLeft(buttonWidth));
            bounds.removeFromLeft(10);

            m_currentTimeLabel.setBounds(bounds.removeFromLeft(timeLabelWidth));

            // To get: CurrentTime | PositionSlider | TotalTime | VolumeSlider
            // 1. Take VolumeSlider from the far right.
            auto volumeSliderSlot = bounds.removeFromRight(volumeSliderWidth);
            m_volumeSlider.setBounds(volumeSliderSlot);
            bounds.removeFromRight(5); // Spacer

            // 2. Take TotalTimeLabel from the new right edge.
            auto totalTimeSlot = bounds.removeFromRight(timeLabelWidth);
            m_totalTimeLabel.setBounds(totalTimeSlot);
            bounds.removeFromRight(5); // Spacer

            // 3. PositionSlider takes the remaining space in the middle.
            m_positionSlider.setBounds(bounds);
        }

        void PlaybackToolbarComponent::buttonClicked(juce::Button *button)
        {
            if (button == &m_playButton)
            { // Play button was clicked
                if (onPlayClicked)
                    onPlayClicked();
            }
            else if (button == &m_pauseButton)
            { // Pause button was clicked
                if (onPauseClicked)
                    onPauseClicked();
            }
            else if (button == &m_stopButton)
            {
                if (onStopClicked)
                    onStopClicked();
            }
        }

        void PlaybackToolbarComponent::setPlayButtonEnabled(bool enabled)
        {
            m_playButton.setEnabled(enabled);
            m_pauseButton.setEnabled(enabled);
        }

        void PlaybackToolbarComponent::setStopButtonEnabled(bool enabled)
        {
            m_stopButton.setEnabled(enabled);
        }

        void PlaybackToolbarComponent::setIsPlaying(bool isPlaying)
        {
            // m_isPlaying = isPlaying; // No need for member m_isPlaying if visibility dictates state

            m_playButton.setVisible(!isPlaying);
            m_pauseButton.setVisible(isPlaying);

            setStopButtonEnabled(isPlaying); // Enable stop only when playing/paused (or just always if desired)
            setPlayButtonEnabled(true);      // Re-enable play/pause buttons generally unless MainComponent disables them
        }
        // Slider Listener Methods:
        void PlaybackToolbarComponent::sliderValueChanged(juce::Slider *slider)
        {
            if (slider == &m_positionSlider)
            {
                if (!m_isPositionSliderBeingDragged) // Only seek if not currently being dragged
                {                                    // (e.g., if user clicks on slider track)
                    if (onPositionSeek)
                    {
                        onPositionSeek(m_positionSlider.getValue());
                    }
                }
                // else: if dragging, just update time display based on slider's current value, don't seek yet.
                // This provides live feedback of the time as user drags.
                if (m_isPositionSliderBeingDragged && onPositionSeek)
                { // Or a different callback for preview
                  // double tempNormalizedPos = m_positionSlider.getValue();
                  // updateTimeDisplay(tempNormalizedPos * m_playbackController.getLengthInSeconds(), m_playbackController.getLengthInSeconds());
                  // This needs access to getLengthInSeconds, maybe via a callback or PlaybackController ref
                }
            }
            else if (slider == &m_volumeSlider)
            {
                if (onVolumeChanged)
                    onVolumeChanged(static_cast<float>(m_volumeSlider.getValue()));
            }
        }

        void PlaybackToolbarComponent::sliderDragEnded(juce::Slider *slider)
        {
            if (slider == &m_positionSlider)
            {
                m_isPositionSliderBeingDragged = false;
                // Perform the seek action now that drag has ended
                if (onPositionSeek)
                {
                    onPositionSeek(m_positionSlider.getValue());
                }
            }
        }

        // Public methods to update UI from MainComponent:
        void PlaybackToolbarComponent::updateTimeDisplay(double currentSeconds, double totalSeconds)
        {
            auto formatTime = [](double seconds)
            {
                if (std::isnan(seconds) || std::isinf(seconds) || seconds < 0)
                    return juce::String{"--:--"};
                int totalSecsInt = static_cast<int>(seconds);
                int mins = totalSecsInt / 60;
                int secs = totalSecsInt % 60;
                return juce::String::formatted("%d:%02d", mins, secs);
            };

            m_currentTimeLabel.setText(formatTime(currentSeconds), juce::dontSendNotification);
            m_totalTimeLabel.setText(formatTime(totalSeconds), juce::dontSendNotification);
        }

        void PlaybackToolbarComponent::setPositionSliderRange(double totalSeconds)
        {
            m_positionSlider.setRange(0.0, std::max(0.1, totalSeconds), 0.001); // Ensure range is not zero
        }

        void PlaybackToolbarComponent::setPositionSliderValue(double currentSeconds)
        {
            // Only update if user is not currently dragging it, to avoid fight
            if (!m_positionSlider.isMouseButtonDown())
            {
                m_positionSlider.setValue(currentSeconds, juce::dontSendNotification);
            }
        }

        void PlaybackToolbarComponent::setVolumeSliderValue(float gain)
        {
            if (!m_volumeSlider.isMouseButtonDown())
            {
                m_volumeSlider.setValue(static_cast<double>(gain), juce::dontSendNotification);
            }
        }

        void PlaybackToolbarComponent::sliderDragStarted(juce::Slider *slider) // Override this
        {
            if (slider == &m_positionSlider)
            {
                m_isPositionSliderBeingDragged = true;
            }
        }
    } // namespace ui
} // namespace jucyaudio