#include <UI/EnhancedPlayerComponent.h>
#include <Utils/UiUtils.h>
#include <BinaryData.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        EnhancedPlayerComponent::EnhancedPlayerComponent(PlaybackController& controller,
                                                       juce::AudioFormatManager& formatManager,
                                                       juce::AudioThumbnailCache& thumbnailCache)
            : m_playbackController(controller),
              m_formatManager(formatManager),
              m_thumbnailCache(thumbnailCache),
              m_waveformDisplay(formatManager, thumbnailCache)
        {
            loadButtonIcons();
            setupButtons();
            setupVolumeControl();
            
            // Set up waveform seek callback
            m_waveformDisplay.onSeek = [this](double normalizedPosition) {
                const auto length = m_playbackController.getLengthInSeconds();
                if (length > 0.0)
                {
                    const double seekTime = normalizedPosition * length;
                    m_playbackController.seek(seekTime);
                }
            };
            
            // Add all components
            addAndMakeVisible(m_previousButton);
            addAndMakeVisible(m_stopButton);
            addAndMakeVisible(m_playButton);
            addAndMakeVisible(m_pauseButton);
            addAndMakeVisible(m_nextButton);
            addAndMakeVisible(m_waveformDisplay);
            
            addAndMakeVisible(m_repeatButton);
            addAndMakeVisible(m_shuffleButton);
            addAndMakeVisible(m_speakerIcon);
            addAndMakeVisible(m_volumeSlider);
            addAndMakeVisible(m_currentTimeLabel);
            addAndMakeVisible(m_totalTimeLabel);
            
            // Start timer for UI updates
            startTimer(50); // 20 FPS update rate
        }
        
        EnhancedPlayerComponent::~EnhancedPlayerComponent()
        {
            stopTimer();
        }
        
        void EnhancedPlayerComponent::paint(juce::Graphics& g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
            
            // Draw separator between rows
            const auto bounds = getLocalBounds();
            const int topRowHeight = static_cast<int>(bounds.getHeight() * 0.7f);
            
            g.setColour(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
            g.drawHorizontalLine(topRowHeight, 0.0f, static_cast<float>(bounds.getWidth()));
            
            // Waveform is painted by the WaveformDisplay component itself
        }
        
        void EnhancedPlayerComponent::resized()
        {
            auto bounds = getLocalBounds();
            const int topRowHeight = static_cast<int>(bounds.getHeight() * 0.7f);
            const int bottomRowHeight = bounds.getHeight() - topRowHeight;
            
            auto topRow = bounds.removeFromTop(topRowHeight);
            auto bottomRow = bounds;
            
            // Top row layout
            const int buttonSize = topRowHeight - 8; // Some padding
            const int buttonPadding = 4;
            const int iconInset = buttonSize / 4; // Make icons 50% of button size
            
            auto transportArea = topRow.removeFromLeft((buttonSize + buttonPadding) * 5);
            transportArea = transportArea.reduced(4);
            
            // Set button bounds with proper sizing
            m_previousButton.setBounds(transportArea.removeFromLeft(buttonSize));
            transportArea.removeFromLeft(buttonPadding);
            m_stopButton.setBounds(transportArea.removeFromLeft(buttonSize));
            transportArea.removeFromLeft(buttonPadding);
            m_playButton.setBounds(transportArea.removeFromLeft(buttonSize));
            transportArea.removeFromLeft(buttonPadding);
            m_pauseButton.setBounds(transportArea.removeFromLeft(buttonSize));
            transportArea.removeFromLeft(buttonPadding);
            m_nextButton.setBounds(transportArea.removeFromLeft(buttonSize));
            
            // Set icon edge insets to make icons smaller within buttons
            m_previousButton.setEdgeIndent(iconInset);
            m_stopButton.setEdgeIndent(iconInset);
            m_playButton.setEdgeIndent(iconInset);
            m_pauseButton.setEdgeIndent(iconInset);
            m_nextButton.setEdgeIndent(iconInset);
            
            // Waveform takes remaining space
            m_waveformDisplay.setBounds(topRow.reduced(4));
            
            // Bottom row layout
            bottomRow = bottomRow.reduced(4, 2);
            
            const int bottomButtonSize = bottomRow.getHeight() - 4;
            
            // Repeat button
            m_repeatButton.setBounds(bottomRow.removeFromLeft(bottomButtonSize + 10));
            bottomRow.removeFromLeft(buttonPadding);
            
            // Shuffle button
            m_shuffleButton.setBounds(bottomRow.removeFromLeft(bottomButtonSize + 10));
            bottomRow.removeFromLeft(buttonPadding * 2);
            
            // Speaker icon
            m_speakerIcon.setBounds(bottomRow.removeFromLeft(bottomButtonSize));
            bottomRow.removeFromLeft(buttonPadding);
            
            // Volume slider
            m_volumeSlider.setBounds(bottomRow.removeFromLeft(100));
            bottomRow.removeFromLeft(buttonPadding * 2);
            
            // Time displays
            const int timeWidth = 60;
            m_currentTimeLabel.setBounds(bottomRow.removeFromLeft(timeWidth));
            bottomRow.removeFromLeft(buttonPadding);
            bottomRow.removeFromLeft(20); // Separator space
            m_totalTimeLabel.setBounds(bottomRow.removeFromLeft(timeWidth));
        }
        
        void EnhancedPlayerComponent::timerCallback()
        {
            updateTransportButtons();
            updateTimeDisplays();
            
            // Update waveform playback position
            const auto length = m_playbackController.getLengthInSeconds();
            if (length > 0.0)
            {
                const auto position = m_playbackController.getCurrentPositionSeconds();
                m_waveformDisplay.setPlaybackPosition(position / length);
            }
        }
        
        void EnhancedPlayerComponent::loadButtonIcons()
        {
            // Load and set button icons
            auto setButtonImage = [](juce::DrawableButton& button, const char* svgData, size_t svgSize)
            {
                std::unique_ptr<juce::Drawable> normal;
                if (auto svg = juce::Drawable::createFromImageData(svgData, svgSize))
                {
                    normal = std::unique_ptr<juce::Drawable>(svg->createCopy());
                    button.setImages(normal.get(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
                }
            };
            
            // Set all transport button icons
            setButtonImage(m_playButton, BinaryData::play_arrow_svg, BinaryData::play_arrow_svgSize);
            setButtonImage(m_pauseButton, BinaryData::pause_svg, BinaryData::pause_svgSize);
            setButtonImage(m_stopButton, BinaryData::stop_svg, BinaryData::stop_svgSize);
            setButtonImage(m_previousButton, BinaryData::skip_previous_svg, BinaryData::skip_previous_svgSize);
            setButtonImage(m_nextButton, BinaryData::skip_next_svg, BinaryData::skip_next_svgSize);
        }
        
        void EnhancedPlayerComponent::setupButtons()
        {
            // Transport buttons
            m_playButton.onClick = [this] { playButtonClicked(); };
            m_pauseButton.onClick = [this] { pauseButtonClicked(); };
            m_stopButton.onClick = [this] { stopButtonClicked(); };
            m_previousButton.onClick = [this] { previousButtonClicked(); };
            m_nextButton.onClick = [this] { nextButtonClicked(); };
            
            // Repeat button
            m_repeatButton.onClick = [this] { repeatButtonClicked(); };
            updateRepeatButton();
            
            // Shuffle button with icon
            updateShuffleButton();
            m_shuffleButton.onClick = [this] { shuffleButtonToggled(); };
            m_shuffleButton.setToggleState(m_shuffleEnabled, juce::dontSendNotification);
        }
        
        void EnhancedPlayerComponent::setupVolumeControl()
        {
            // Volume icon
            updateVolumeIcon(m_playbackController.getTransportSource().getGain());
            m_speakerIcon.setJustificationType(juce::Justification::centred);
            
            // Volume slider
            m_volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
            m_volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            m_volumeSlider.setRange(0.0, 1.0, 0.01);
            m_volumeSlider.setValue(m_playbackController.getTransportSource().getGain());
            
            m_volumeSlider.onValueChange = [this]
            {
                const float gain = static_cast<float>(m_volumeSlider.getValue());
                m_playbackController.setGain(gain);
                updateVolumeIcon(gain);
            };
            
            // Time labels
            m_currentTimeLabel.setText("0:00", juce::dontSendNotification);
            m_currentTimeLabel.setJustificationType(juce::Justification::centred);
            
            m_totalTimeLabel.setText("0:00", juce::dontSendNotification);
            m_totalTimeLabel.setJustificationType(juce::Justification::centred);
        }
        
        void EnhancedPlayerComponent::updateTransportButtons()
        {
            const auto state = m_playbackController.getCurrentState();
            const bool hasFile = !m_playbackController.getCurrentFilepath().isEmpty();
            
            m_playButton.setEnabled(hasFile && state != PlaybackController::State::Playing);
            m_pauseButton.setEnabled(state == PlaybackController::State::Playing);
            m_stopButton.setEnabled(hasFile && state != PlaybackController::State::Stopped);
            m_previousButton.setEnabled(hasFile);
            m_nextButton.setEnabled(hasFile);
        }
        
        void EnhancedPlayerComponent::updateTimeDisplays()
        {
            if (m_playbackController.getCurrentFilepath().isEmpty())
            {
                m_currentTimeLabel.setText("0:00", juce::dontSendNotification);
                m_totalTimeLabel.setText("0:00", juce::dontSendNotification);
                return;
            }
            
            const auto position = m_playbackController.getCurrentPositionSeconds();
            const auto length = m_playbackController.getLengthInSeconds();
            
            auto formatTime = [](double seconds) -> juce::String
            {
                const int hours = static_cast<int>(seconds) / 3600;
                const int minutes = (static_cast<int>(seconds) % 3600) / 60;
                const int secs = static_cast<int>(seconds) % 60;
                
                if (hours > 0)
                {
                    return juce::String::formatted("%d:%02d:%02d", hours, minutes, secs);
                }
                else
                {
                    return juce::String::formatted("%d:%02d", minutes, secs);
                }
            };
            
            m_currentTimeLabel.setText(formatTime(position), juce::dontSendNotification);
            m_totalTimeLabel.setText(formatTime(length), juce::dontSendNotification);
        }
        
        void EnhancedPlayerComponent::updateRepeatButton()
        {
            switch (m_repeatMode)
            {
                case RepeatMode::Off:
                m_repeatButton.setButtonText("Repeat");
                    m_repeatButton.setToggleState(false, juce::dontSendNotification);
                    break;
                case RepeatMode::RepeatOne:
                    m_repeatButton.setButtonText("Repeat 1");
                    m_repeatButton.setToggleState(true, juce::dontSendNotification);
                    break;
                case RepeatMode::RepeatAll:
                    m_repeatButton.setButtonText("Repeat All");
                    m_repeatButton.setToggleState(true, juce::dontSendNotification);
                    break;
            }
        }
        
        void EnhancedPlayerComponent::playButtonClicked()
        {
            m_playbackController.play();
        }
        
        void EnhancedPlayerComponent::pauseButtonClicked()
        {
            // Pause button only pauses, doesn't resume
            m_playbackController.pause();
        }
        
        void EnhancedPlayerComponent::stopButtonClicked()
        {
            m_playbackController.stop();
        }
        
        void EnhancedPlayerComponent::previousButtonClicked()
        {
            if (onPreviousTrack)
            {
                onPreviousTrack();
            }
        }
        
        void EnhancedPlayerComponent::nextButtonClicked()
        {
            if (onNextTrack)
            {
                onNextTrack();
            }
        }
        
        void EnhancedPlayerComponent::repeatButtonClicked()
        {
            // Cycle through repeat modes
            switch (m_repeatMode)
            {
                case RepeatMode::Off:
                    m_repeatMode = RepeatMode::RepeatOne;
                    break;
                case RepeatMode::RepeatOne:
                    m_repeatMode = RepeatMode::RepeatAll;
                    break;
                case RepeatMode::RepeatAll:
                    m_repeatMode = RepeatMode::Off;
                    break;
            }
            
            updateRepeatButton();
            spdlog::info("Repeat mode changed to: {}", static_cast<int>(m_repeatMode));
        }
        
        void EnhancedPlayerComponent::shuffleButtonToggled()
        {
            m_shuffleEnabled = m_shuffleButton.getToggleState();
            updateShuffleButton();
            spdlog::info("Shuffle {}", m_shuffleEnabled ? "enabled" : "disabled");
        }
        
        void EnhancedPlayerComponent::updateShuffleButton()
        {
            // Update shuffle button appearance based on state
            if (m_shuffleEnabled)
            {
                m_shuffleButton.setButtonText("Shuffle On");
            }
            else
            {
                m_shuffleButton.setButtonText("Shuffle");
            }
        }
        
        void EnhancedPlayerComponent::updateVolumeIcon(float gain)
        {
            if (gain <= 0.0f)
            {
                m_speakerIcon.setText("Mute", juce::dontSendNotification);
            }
            else if (gain < 0.5f)
            {
                m_speakerIcon.setText("Vol-", juce::dontSendNotification);
            }
            else
            {
                m_speakerIcon.setText("Vol+", juce::dontSendNotification);
            }
        }
        
        void EnhancedPlayerComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
        {
            // Handle thumbnail changes if needed
            repaint();
        }
        
        void EnhancedPlayerComponent::loadFile(const juce::File& file)
        {
            m_waveformDisplay.loadFile(file);
        }
        
        // WaveformDisplay implementation
        EnhancedPlayerComponent::WaveformDisplay::WaveformDisplay(juce::AudioFormatManager& formatManager,
                                                                 juce::AudioThumbnailCache& thumbnailCache)
            : m_thumbnail(512, formatManager, thumbnailCache)
        {
            m_thumbnail.addChangeListener(this);
        }
        
        EnhancedPlayerComponent::WaveformDisplay::~WaveformDisplay()
        {
            m_thumbnail.removeChangeListener(this);
        }
        
        void EnhancedPlayerComponent::WaveformDisplay::paint(juce::Graphics& g)
        {
            auto bounds = getLocalBounds();
            auto& lf = getLookAndFeel();
            
            // Background
            g.setColour(lf.findColour(juce::TextEditor::backgroundColourId));
            g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
            
            if (!m_fileLoaded)
            {
                g.setColour(lf.findColour(juce::Label::textColourId).withAlpha(0.5f));
                g.drawText("No track loaded", bounds, juce::Justification::centred, false);
                return;
            }
            
            // Draw waveform in two colors - played portion and unplayed portion
            const double totalLength = m_thumbnail.getTotalLength();
            if (totalLength > 0)
            {
                auto waveformBounds = bounds.reduced(2);
                
                // Draw unplayed portion first (full waveform)
                g.setColour(lf.findColour(juce::Slider::thumbColourId).withAlpha(0.5f));
                m_thumbnail.drawChannel(g, waveformBounds,
                                      0.0, totalLength,
                                      0, 1.0f);
                
                // Draw played portion on top
                if (m_playbackPosition > 0.0)
                {
                    const int playedWidth = static_cast<int>(waveformBounds.getWidth() * m_playbackPosition);
                    auto playedBounds = waveformBounds.withWidth(playedWidth);
                    
                    g.saveState();
                    g.reduceClipRegion(playedBounds);
                    g.setColour(lf.findColour(juce::Slider::thumbColourId));
                    m_thumbnail.drawChannel(g, waveformBounds,
                                          0.0, totalLength,
                                          0, 1.0f);
                    g.restoreState();
                }
                
                // Draw playhead line
                if (m_playbackPosition > 0.0 && m_playbackPosition < 1.0)
                {
                    const int playheadX = waveformBounds.getX() + static_cast<int>(waveformBounds.getWidth() * m_playbackPosition);
                    g.setColour(juce::Colours::white);
                    g.drawVerticalLine(playheadX, waveformBounds.getY(), waveformBounds.getBottom());
                }
            }
            
            // Border
            g.setColour(lf.findColour(juce::ComboBox::outlineColourId));
            g.drawRoundedRectangle(bounds.toFloat().reduced(1), 4.0f, 1.0f);
        }
        
        void EnhancedPlayerComponent::WaveformDisplay::mouseDown(const juce::MouseEvent& event)
        {
            if (!m_fileLoaded || !onSeek)
                return;
                
            const double clickPosition = event.position.x / static_cast<double>(getWidth());
            const double seekTime = juce::jlimit(0.0, 1.0, clickPosition);
            onSeek(seekTime);
        }
        
        void EnhancedPlayerComponent::WaveformDisplay::loadFile(const juce::File& file)
        {
            if (file.existsAsFile())
            {
                m_thumbnail.setSource(new juce::FileInputSource(file));
                m_fileLoaded = true;
                m_playbackPosition = 0.0;
            }
            else
            {
                m_thumbnail.clear();
                m_fileLoaded = false;
            }
            repaint();
        }
        
        void EnhancedPlayerComponent::WaveformDisplay::setPlaybackPosition(double position)
        {
            if (m_playbackPosition != position)
            {
                m_playbackPosition = juce::jlimit(0.0, 1.0, position);
                repaint();
            }
        }
        
        void EnhancedPlayerComponent::WaveformDisplay::changeListenerCallback(juce::ChangeBroadcaster* source)
        {
            if (source == &m_thumbnail)
            {
                repaint();
            }
        }
    }
}