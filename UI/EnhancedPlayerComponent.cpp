#include <BinaryData.h>
#include <UI/CustomColourIds.h>
#include <UI/EnhancedPlayerComponent.h>
#include <Utils/UiUtils.h>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        EnhancedPlayerComponent::EnhancedPlayerComponent(
            PlaybackController &controller, juce::AudioFormatManager &formatManager, juce::AudioThumbnailCache &thumbnailCache)
            : m_waveformDisplay{formatManager, thumbnailCache},
              m_playbackController{controller}
        {
            loadButtonIcons();
            loadVolumeIcons();
            setupButtons();
            setupVolumeControl();

            // Set up waveform seek callback
            m_waveformDisplay.onSeek = [this](double normalizedPosition)
            {
                const auto length = m_playbackController.getLengthInSeconds();
                if (length > 0.0)
                {
                    const double seekTime = normalizedPosition * length;
                    m_playbackController.seek(seekTime);
                }
            };

            // Set up marker callback
            m_waveformDisplay.onMarkerClicked = [this](std::chrono::milliseconds position)
            {
                if (m_currentTrackId && onMarkerAction)
                {
                    // Check if this is a new marker position or existing marker
                    const auto &markers = m_waveformDisplay.getMarkers();
                    const auto isNewMarker = std::none_of(markers.begin(),
                        markers.end(),
                        [position](const auto &marker)
                        {
                            return marker.position == position;
                        });
                    spdlog::info("Marker action: trackId={}, position={}ms, isNew={}", *m_currentTrackId, position.count(), isNewMarker);
                    onMarkerAction(*m_currentTrackId, position, isNewMarker);
                }
                else
                {
                    spdlog::warn("Cannot handle marker action: trackId={}, callback set={}", m_currentTrackId.has_value(), onMarkerAction != nullptr);
                }
            };

            // Add all components (transport buttons)
            addAndMakeVisible(m_prevButton);
            addAndMakeVisible(m_stopButton);
            addAndMakeVisible(m_playPauseButton);
            addAndMakeVisible(m_nextButton);
            addAndMakeVisible(m_waveformDisplay);

            addAndMakeVisible(m_volumeButton);
            addAndMakeVisible(m_volumeSlider);
            addAndMakeVisible(m_trackInfoLabel);
            addAndMakeVisible(m_currentTimeLabel);
            addAndMakeVisible(m_totalTimeLabel);
            addAndMakeVisible(m_repeatButton);
            addAndMakeVisible(m_shuffleButton);

            m_trackInfoLabel.setText("No track loaded", juce::dontSendNotification);
            m_trackInfoLabel.setJustificationType(juce::Justification::centredLeft);
            // Let the label inherit its text color from the theme

            // Initialize button states from PlaybackController
            m_isRepeatOn = m_playbackController.getRepeatMode();
            m_isShuffleOn = m_playbackController.getShuffleMode();
            updateToggleButtons();
        }

        EnhancedPlayerComponent::~EnhancedPlayerComponent()
        {
        }

        void EnhancedPlayerComponent::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        }

        void EnhancedPlayerComponent::setRightHandPadding(int padding)
        {
            if (m_rightHandPadding != padding)
            {
                m_rightHandPadding = padding;
                resized(); // Trigger a layout update
            }
        }

        void EnhancedPlayerComponent::lookAndFeelChanged()
        {
            // Reload icons with new accent color when theme changes
            loadButtonIcons();
            loadVolumeIcons();
            updateToggleButtons(); // Refresh toggle button states
            repaint();
        }

        void EnhancedPlayerComponent::resized()
        {
            auto bounds = getLocalBounds();
            bounds.removeFromRight(m_rightHandPadding);

            const int topRowHeight = static_cast<int>(bounds.getHeight() * 0.7f);

            auto topRow = bounds.removeFromTop(topRowHeight);
            auto bottomRow = bounds;

            // --- Top row layout (correct and unchanged) ---
            const int buttonSize = topRowHeight - 8;
            const int iconInset = buttonSize / 4;
            auto transportArea = topRow.removeFromLeft(buttonSize * 4);
            topRow.removeFromLeft(8);
            m_prevButton.setBounds(transportArea.removeFromLeft(buttonSize));
            m_stopButton.setBounds(transportArea.removeFromLeft(buttonSize));
            m_playPauseButton.setBounds(transportArea.removeFromLeft(buttonSize));
            m_nextButton.setBounds(transportArea.removeFromLeft(buttonSize));
            m_prevButton.setEdgeIndent(iconInset);
            m_stopButton.setEdgeIndent(iconInset);
            m_playPauseButton.setEdgeIndent(iconInset);
            m_nextButton.setEdgeIndent(iconInset);
            m_waveformDisplay.setBounds(topRow);

            // --- Bottom row layout (New, simple, and robust design) ---
            auto bottomArea = bottomRow.reduced(4, 2);

            const int itemHeight = bottomArea.getHeight();
            const int buttonPadding = 4;
            const int timeWidth = 55;
            const int sliderWidth = 100; // Fixed slider width as you suggested

            // 1. Layout fixed-width components from the right.
            m_shuffleButton.setBounds(bottomArea.removeFromRight(itemHeight));
            m_shuffleButton.setEdgeIndent(itemHeight / 5);
            bottomArea.removeFromRight(buttonPadding);
            m_repeatButton.setBounds(bottomArea.removeFromRight(itemHeight));
            m_repeatButton.setEdgeIndent(itemHeight / 5);
            bottomArea.removeFromRight(buttonPadding * 2);
            m_totalTimeLabel.setBounds(bottomArea.removeFromRight(timeWidth));
            bottomArea.removeFromRight(buttonPadding);
            m_currentTimeLabel.setBounds(bottomArea.removeFromRight(timeWidth));

            // 2. Layout fixed-width components from the left.
            m_volumeButton.setBounds(bottomArea.removeFromLeft(itemHeight));
            m_volumeButton.setEdgeIndent(itemHeight / 5);
            bottomArea.removeFromLeft(buttonPadding);
            m_volumeSlider.setBounds(bottomArea.removeFromLeft(sliderWidth));

            // 3. The new label takes all remaining space.
            bottomArea.removeFromLeft(buttonPadding * 2);
            bottomArea.removeFromRight(buttonPadding * 2);
            m_trackInfoLabel.setBounds(bottomArea);
        }

        void EnhancedPlayerComponent::updatePlaybackPosition()
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
            const auto originalIconColour = juce::Colour(0xFFF96E00);  // Standardized orange in all SVGs

            const bool isThemeLoaded = getLookAndFeel().isColourSpecified(jucyaudio::ui::accentColourId);
            const auto accentColour = isThemeLoaded ? findColour(jucyaudio::ui::accentColourId) : originalIconColour;

            auto loadSvg = [&](const char *data, size_t size)
            {
                auto drawable = juce::Drawable::createFromImageData(data, size);
                if (drawable && accentColour != originalIconColour)
                    drawable->replaceColour(originalIconColour, accentColour);
                return drawable;
            };

            // --- Play/Pause Button ---
            m_iconPlay = loadSvg(BinaryData::play_arrow_svg, BinaryData::play_arrow_svgSize);
            m_iconPause = loadSvg(BinaryData::pause_svg, BinaryData::pause_svgSize);
            m_playPauseButton.setImages(m_iconPlay.get(), nullptr, nullptr, nullptr, m_iconPause.get());
            
            // Make button background transparent so parent background shows through
            m_playPauseButton.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentWhite);
            m_playPauseButton.setColour(juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentWhite);

            // --- Simple Transport Buttons ---
            auto setSimpleButtonImage = [&](juce::DrawableButton &button, const char *data, size_t size)
            {
                button.setImages(loadSvg(data, size).get());
                // Make backgrounds transparent
                button.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentWhite);
                button.setColour(juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentWhite);
            };
            setSimpleButtonImage(m_prevButton, BinaryData::prev_svg, BinaryData::prev_svgSize);
            setSimpleButtonImage(m_stopButton, BinaryData::stop_svg, BinaryData::stop_svgSize);
            setSimpleButtonImage(m_nextButton, BinaryData::next_svg, BinaryData::next_svgSize);

            // --- Repeat and Shuffle Buttons with On/Off Colors ---
            const auto offColour = findColour(juce::Label::textColourId).withAlpha(0.7f);

            // =================================================================================
            // CRITICAL ASSUMPTION:
            // The following code assumes that the source SVG files (repeat.svg, shuffle.svg)
            // are designed with a single, consistent color that we can target for replacement.
            // The SVGs currently use orange (#ff8e31), so we need to replace that color.
            // =================================================================================
            const auto originalSvgColour = juce::Colour(0xFFFF8E31);  // The actual color in repeat/shuffle SVGs

            auto createColouredIcon = [&](const char *data, size_t size, juce::Colour newColour)
            {
                auto icon = loadSvg(data, size);
                if (icon)
                    icon->replaceColour(originalSvgColour, newColour);
                return icon;
            };

            // Create the 'off' and 'on' versions of the repeat icon
            m_iconRepeatOff = createColouredIcon(BinaryData::repeat_svg, BinaryData::repeat_svgSize, offColour);
            m_iconRepeatOn = createColouredIcon(BinaryData::repeat_svg, BinaryData::repeat_svgSize, accentColour);

            // Create the 'off' and 'on' versions of the shuffle icon
            m_iconShuffleOff = createColouredIcon(BinaryData::shuffle_svg, BinaryData::shuffle_svgSize, offColour);
            m_iconShuffleOn = createColouredIcon(BinaryData::shuffle_svg, BinaryData::shuffle_svgSize, accentColour);

            // Set the button images: 'off' for normal, 'on' for toggled
            m_repeatButton.setImages(m_iconRepeatOff.get(), nullptr, nullptr, nullptr, m_iconRepeatOn.get());
            m_shuffleButton.setImages(m_iconShuffleOff.get(), nullptr, nullptr, nullptr, m_iconShuffleOn.get());
            
            // Make backgrounds transparent
            m_repeatButton.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentWhite);
            m_repeatButton.setColour(juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentWhite);
            m_shuffleButton.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentWhite);
            m_shuffleButton.setColour(juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentWhite);
        }
        void EnhancedPlayerComponent::loadVolumeIcons()
        {
            const auto originalIconColour = juce::Colour(0xFFF96E00);  // Standardized orange in all SVGs
            const bool isThemeLoaded = getLookAndFeel().isColourSpecified(jucyaudio::ui::accentColourId);
            const auto accentColour = isThemeLoaded ? findColour(jucyaudio::ui::accentColourId) : originalIconColour;
            
            auto loadSvg = [&](const char *data, size_t size)
            {
                auto drawable = juce::Drawable::createFromImageData(data, size);
                if (drawable && accentColour != originalIconColour)
                    drawable->replaceColour(originalIconColour, accentColour);
                return drawable;
            };

            m_iconVolumeHigh = loadSvg(BinaryData::highaudio_svg, BinaryData::highaudio_svgSize);
            m_iconVolumeLow = loadSvg(BinaryData::lowaudio_svg, BinaryData::lowaudio_svgSize);
            m_iconVolumeMute = loadSvg(BinaryData::mute_svg, BinaryData::mute_svgSize);
        }

        void EnhancedPlayerComponent::setupButtons()
        {
            // Set tooltips for all buttons
            m_prevButton.setTooltip("Previous Track");
            m_stopButton.setTooltip("Stop");
            m_playPauseButton.setTooltip("Play/Pause");
            m_nextButton.setTooltip("Next Track");
            m_volumeButton.setTooltip("Mute/Unmute");
            m_repeatButton.setTooltip("Toggle Repeat");
            m_shuffleButton.setTooltip("Toggle Shuffle");
            
            // Make volume button background transparent
            m_volumeButton.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentWhite);
            m_volumeButton.setColour(juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentWhite);

            m_prevButton.onClick = [this]
            {
                if (onPreviousTrack)
                    onPreviousTrack();
            };
            m_stopButton.onClick = [this]
            {
                stopButtonClicked();
            };
            m_nextButton.onClick = [this]
            {
                if (onNextTrack)
                    onNextTrack();
            };
            m_volumeButton.onClick = [this]
            {
                volumeButtonClicked();
            };

            // Setup the combined play/pause button
            m_playPauseButton.setClickingTogglesState(true);
            m_playPauseButton.onClick = [this]
            {
                playPauseButtonClicked();
            };

            // Setup new toggle buttons
            m_repeatButton.setClickingTogglesState(true);
            m_repeatButton.onClick = [this]
            {
                repeatButtonClicked();
            };

            m_shuffleButton.setClickingTogglesState(true);
            m_shuffleButton.onClick = [this]
            {
                shuffleButtonClicked();
            };
        }

        void EnhancedPlayerComponent::setupVolumeControl()
        {
            m_volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
            m_volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            m_volumeSlider.setRange(0.0, 1.0, 0.01);
            m_volumeSlider.setValue(m_playbackController.getTransportSource().getGain());
            m_volumeSlider.setTooltip("Volume");

            m_volumeSlider.onValueChange = [this]
            {
                const float gain = static_cast<float>(m_volumeSlider.getValue());
                m_playbackController.setGain(gain);
                updateVolumeIcon(gain);
            };

            m_currentTimeLabel.setText("0:00", juce::dontSendNotification);
            m_currentTimeLabel.setJustificationType(juce::Justification::centred);
            m_currentTimeLabel.setTooltip("Current Position");

            m_totalTimeLabel.setText("0:00", juce::dontSendNotification);
            m_totalTimeLabel.setJustificationType(juce::Justification::centred);
            m_totalTimeLabel.setTooltip("Total Duration");

            updateVolumeIcon(m_playbackController.getTransportSource().getGain());
        }

        void EnhancedPlayerComponent::repeatButtonClicked()
        {
            m_isRepeatOn = !m_isRepeatOn;
            m_playbackController.setRepeatMode(m_isRepeatOn);
            updateToggleButtons();
            spdlog::info("Repeat mode {}", m_isRepeatOn ? "enabled" : "disabled");
        }

        void EnhancedPlayerComponent::shuffleButtonClicked()
        {
            m_isShuffleOn = !m_isShuffleOn;
            m_playbackController.setShuffleMode(m_isShuffleOn);
            updateToggleButtons();
            spdlog::info("Shuffle mode {}", m_isShuffleOn ? "enabled" : "disabled");
        }

        void EnhancedPlayerComponent::updateTransportButtons()
        {
            using PlayerState = PlaybackController::PlayerState;
            const auto state = m_playbackController.getState();

            const bool isTrackLoaded = (state == PlayerState::SilenceTrackLoaded || state == PlayerState::TrackPlaying || state == PlayerState::TrackPaused);

            const bool isMixLoaded = (state == PlayerState::SilenceMixLoaded || state == PlayerState::MixPlaying || state == PlayerState::MixPaused);

            const bool isPlaying = (state == PlayerState::TrackPlaying || state == PlayerState::MixPlaying);
            const bool isPaused = (state == PlayerState::TrackPaused || state == PlayerState::MixPaused);

            // The Play/Pause button is enabled whenever a track or mix is loaded.
            m_playPauseButton.setEnabled(isTrackLoaded || isMixLoaded);

            // The button's visual state (play or pause icon) depends on whether we are playing.
            m_playPauseButton.setToggleState(isPlaying, juce::dontSendNotification);

            // Stop button is enabled if we are playing or paused.
            m_stopButton.setEnabled(isPlaying || isPaused);
        }

        void EnhancedPlayerComponent::updateToggleButtons()
        {
            m_repeatButton.setEnabled(true);
            m_repeatButton.setToggleState(m_isRepeatOn, juce::dontSendNotification);
            m_shuffleButton.setEnabled(true);
            m_shuffleButton.setToggleState(m_isShuffleOn, juce::dontSendNotification);
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

        void EnhancedPlayerComponent::playPauseButtonClicked()
        {
            if (m_playbackController.getState() == PlaybackController::PlayerState::TrackPlaying ||
                m_playbackController.getState() == PlaybackController::PlayerState::MixPlaying)
            {
                m_playbackController.pause();
            }
            else
            {
                m_playbackController.play();
            }
        }

        void EnhancedPlayerComponent::stopButtonClicked()
        {
            m_playbackController.stop();
        }

        void EnhancedPlayerComponent::volumeButtonClicked()
        {
            const float currentGain = static_cast<float>(m_volumeSlider.getValue());
            if (currentGain > 0.0f)
            {
                m_lastVolumeBeforeMute = currentGain;
                m_volumeSlider.setValue(0.0, juce::sendNotification);
            }
            else
            {
                m_volumeSlider.setValue(m_lastVolumeBeforeMute, juce::sendNotification);
            }
        }

        void EnhancedPlayerComponent::updateVolumeIcon(float gain)
        {
            if (gain <= 0.0f)
            {
                m_volumeButton.setImages(m_iconVolumeMute.get());
            }
            else if (gain < 0.5f)
            {
                m_volumeButton.setImages(m_iconVolumeLow.get());
            }
            else
            {
                m_volumeButton.setImages(m_iconVolumeHigh.get());
            }
        }

        void EnhancedPlayerComponent::changeListenerCallback(juce::ChangeBroadcaster *source)
        {
            // Handle thumbnail changes if needed
            repaint();
        }

        void EnhancedPlayerComponent::loadFile(const juce::File &file, std::string_view text, std::optional<TrackId> trackId)
        {
            m_waveformDisplay.loadFile(file);
            m_currentTrackId = trackId;

            // Clear markers if no track ID provided
            if (!trackId)
            {
                m_waveformDisplay.setMarkers({});
                setTrackInfo({});
            }
            else
            {
                setTrackInfo(std::string{text});
            }
            // We'll load markers from the database when we connect it to MainComponent
        }

        void EnhancedPlayerComponent::setTrackInfo(const juce::String &info)
        {
            if (info.isNotEmpty())
            {
                m_trackInfoLabel.setText(info, juce::dontSendNotification);
            }
            else
            {
                m_trackInfoLabel.setText("No track loaded", juce::dontSendNotification);
            }
        }
        void EnhancedPlayerComponent::setMarkers(const std::vector<database::TrackMarker> &markers)
        {
            m_waveformDisplay.setMarkers(markers);
        }

        float EnhancedPlayerComponent::getVolumeSliderValue() const
        {
            return static_cast<float>(m_volumeSlider.getValue());
        }

        void EnhancedPlayerComponent::setVolumeSliderValue(float value)
        {
            m_volumeSlider.setValue(value, juce::sendNotification);
        }

        // WaveformDisplay implementation
        EnhancedPlayerComponent::WaveformDisplay::WaveformDisplay(juce::AudioFormatManager &formatManager, juce::AudioThumbnailCache &thumbnailCache)
            : m_thumbnail(512, formatManager, thumbnailCache)
        {
            m_thumbnail.addChangeListener(this);
        }

        EnhancedPlayerComponent::WaveformDisplay::~WaveformDisplay()
        {
            m_thumbnail.removeChangeListener(this);
        }

        void EnhancedPlayerComponent::WaveformDisplay::paint(juce::Graphics &g)
        {
            auto bounds = getLocalBounds();
            auto &lf = getLookAndFeel();

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
                g.setColour(lf.findColour(jucyaudio::ui::waveformColourId).withAlpha(0.5f));
                m_thumbnail.drawChannel(g, waveformBounds, 0.0, totalLength, 0, 1.0f);

                // Draw played portion on top
                if (m_playbackPosition > 0.0)
                {
                    const int playedWidth = static_cast<int>(waveformBounds.getWidth() * m_playbackPosition);
                    auto playedBounds = waveformBounds.withWidth(playedWidth);

                    g.saveState();
                    g.reduceClipRegion(playedBounds);
                    g.setColour(lf.findColour(jucyaudio::ui::waveformColourId));
                    m_thumbnail.drawChannel(g, waveformBounds, 0.0, totalLength, 0, 1.0f);
                    g.restoreState();
                }

                // Draw playhead line
                if (m_playbackPosition > 0.0 && m_playbackPosition < 1.0)
                {
                    const auto playheadX = waveformBounds.getX() + static_cast<int>(waveformBounds.getWidth() * m_playbackPosition);
                    g.setColour(juce::Colours::white);
                    g.drawVerticalLine(playheadX, waveformBounds.getY(), waveformBounds.getBottom());
                }

                // Draw markers
                for (size_t i = 0; i < m_markers.size(); ++i)
                {
                    const auto &marker = m_markers[i];
                    const auto markerX = markerPositionToScreenX(marker);

                    // Draw marker line
                    const auto isHovered = m_hoveredMarkerIndex && *m_hoveredMarkerIndex == i;
                    g.setColour(isHovered ? juce::Colours::yellow : juce::Colours::orange);
                    g.drawVerticalLine(markerX, waveformBounds.getY(), waveformBounds.getBottom());

                    // Draw marker triangle at top
                    juce::Path triangle;
                    const auto triangleSize = isHovered ? 8.0f : 6.0f;
                    triangle.addTriangle(markerX - triangleSize / 2,
                        waveformBounds.getY(),
                        markerX + triangleSize / 2,
                        waveformBounds.getY(),
                        markerX,
                        waveformBounds.getY() + triangleSize);
                    g.fillPath(triangle);
                }
            }

            // Border
            g.setColour(lf.findColour(juce::ComboBox::outlineColourId));
            g.drawRoundedRectangle(bounds.toFloat().reduced(1), 4.0f, 1.0f);
        }

        void EnhancedPlayerComponent::WaveformDisplay::mouseDown(const juce::MouseEvent &event)
        {
            if (!m_fileLoaded)
            {
                spdlog::debug("WaveformDisplay::mouseDown - no file loaded");
                return;
            }

            spdlog::debug("WaveformDisplay::mouseDown - mods: Ctrl={}, Shift={}, Alt={}, Cmd={}",
                event.mods.isCtrlDown(),
                event.mods.isShiftDown(),
                event.mods.isAltDown(),
                event.mods.isCommandDown());

            // Check if clicking on a marker
            const auto markerHit = hitTestMarker(event.position.toInt());
            if (markerHit)
            {
                spdlog::info("Clicked on marker {} at position {}ms", *markerHit, m_markers[*markerHit].position.count());
                if (onMarkerClicked)
                {
                    onMarkerClicked(m_markers[*markerHit].position);
                }
                return;
            }

            // Check for Ctrl+Click to create marker (use isCtrlDown for Windows/Linux)
            if (event.mods.isCtrlDown())
            {
                const double clickPosition = event.position.x / static_cast<double>(getWidth());
                const auto positionMs = std::chrono::milliseconds(static_cast<int64_t>(clickPosition * m_thumbnail.getTotalLength() * 1000));
                spdlog::info("Ctrl+Click detected at {}ms - creating marker", positionMs.count());

                if (onMarkerClicked)
                {
                    onMarkerClicked(positionMs);
                }
                else
                {
                    spdlog::warn("onMarkerClicked callback not set!");
                }
                return;
            }

            // Normal click - seek
            if (onSeek)
            {
                const double clickPosition = event.position.x / static_cast<double>(getWidth());
                const double seekTime = juce::jlimit(0.0, 1.0, clickPosition);
                spdlog::debug("Normal click - seeking to {}%", seekTime * 100);
                onSeek(seekTime);
            }
        }

        void EnhancedPlayerComponent::WaveformDisplay::loadFile(const juce::File &file)
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

        void EnhancedPlayerComponent::WaveformDisplay::changeListenerCallback(juce::ChangeBroadcaster *source)
        {
            if (source == &m_thumbnail)
            {
                repaint();
            }
        }

        void EnhancedPlayerComponent::WaveformDisplay::setMarkers(const std::vector<database::TrackMarker> &markers)
        {
            m_markers = markers;
            m_hoveredMarkerIndex.reset();
            repaint();
        }

        void EnhancedPlayerComponent::WaveformDisplay::mouseMove(const juce::MouseEvent &event)
        {
            const auto newHoveredIndex = hitTestMarker(event.position.toInt());
            if (newHoveredIndex != m_hoveredMarkerIndex)
            {
                m_hoveredMarkerIndex = newHoveredIndex;
                repaint();

                // Update cursor and tooltip
                if (newHoveredIndex.has_value())
                {
                    setMouseCursor(juce::MouseCursor::PointingHandCursor);

                    // Format tooltip with marker information
                    const auto &marker = m_markers[*newHoveredIndex];
                    const auto positionStr = formatMarkerPosition(marker.position);
                    m_currentTooltip = positionStr + "\n" + marker.comment;
                }
                else
                {
                    setMouseCursor(juce::MouseCursor::NormalCursor);
                    m_currentTooltip.clear();
                }
            }
        }

        void EnhancedPlayerComponent::WaveformDisplay::mouseExit(const juce::MouseEvent &event)
        {
            m_hoveredMarkerIndex.reset();
            m_currentTooltip.clear();
            setMouseCursor(juce::MouseCursor::NormalCursor);
            repaint();
        }

        int EnhancedPlayerComponent::WaveformDisplay::markerPositionToScreenX(const database::TrackMarker &marker) const
        {
            const auto totalLength = m_thumbnail.getTotalLength();
            if (totalLength <= 0.0)
                return 0;

            const auto markerSeconds = std::chrono::duration<double>(marker.position).count();
            const auto normalizedPosition = markerSeconds / totalLength;
            return static_cast<int>(getWidth() * normalizedPosition);
        }

        std::optional<size_t> EnhancedPlayerComponent::WaveformDisplay::hitTestMarker(juce::Point<int> pos) const
        {
            constexpr auto hitRadius = 5;

            for (size_t i = 0; i < m_markers.size(); ++i)
            {
                const auto markerX = markerPositionToScreenX(m_markers[i]);
                if (std::abs(pos.x - markerX) <= hitRadius)
                {
                    return i;
                }
            }

            return std::nullopt;
        }

        juce::String EnhancedPlayerComponent::WaveformDisplay::formatMarkerPosition(std::chrono::milliseconds position) const
        {
            const auto totalSeconds = position.count() / 1000;
            const auto minutes = totalSeconds / 60;
            const auto seconds = totalSeconds % 60;

            return juce::String::formatted("%d:%02d", static_cast<int>(minutes), static_cast<int>(seconds));
        }
    } // namespace ui
} // namespace jucyaudio