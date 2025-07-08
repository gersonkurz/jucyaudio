#include "BinaryData.h"
#include <Database/Includes/Constants.h>
#include <UI/MixTrackComponent.h>
#include <UI/TimelineComponent.h>
#include <Utils/AssortedUtils.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        MixTrackComponent::MixTrackComponent(const database::MixTrack &mixTrack, const database::TrackInfo &trackInfo, juce::AudioFormatManager &formatManager,
                                             juce::AudioThumbnailCache &thumbnailCache)
            : m_mixTrack{mixTrack},
              m_trackInfo{trackInfo},
              m_thumbnail{512, formatManager, thumbnailCache}
        {
            // Setup the info label
            juce::String bpmText = trackInfo.bpm.has_value() ? juce::String(trackInfo.bpm.value() / 100.0, 1) + " BPM" : "--- BPM";

            juce::String infoText = juce::String(trackInfo.title) + " (" + bpmText + ")";
            m_infoLabel.setText(infoText, juce::dontSendNotification);
            m_infoLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(14.0f)}.boldened());
            m_infoLabel.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(m_infoLabel);

            // Load the thumbnail source
            m_thumbnail.setSource(new juce::FileInputSource(juce::File(trackInfo.filepath.string())));
            m_thumbnail.addChangeListener(this);

                // Set up drag constraints for horizontal-only movement
            m_constrainer.setMinimumOnscreenAmounts(0xffffff, 0xffffff, 0xffffff, 0xffffff);

            // Restrict to horizontal movement only by setting fixed Y position
            // We'll update this in resized() to match the actual Y position
            m_constrainer.setFixedAspectRatio(0.0); // Allow any aspect ratio
        }

        MixTrackComponent::~MixTrackComponent()
        {
            m_thumbnail.removeChangeListener(this);
        }

        void MixTrackComponent::mouseDown(const juce::MouseEvent &event)
        {
            if (event.mods.isLeftButtonDown())
            {
                if (auto *timeline = findParentComponentOfClass<TimelineComponent>())
                {
                    timeline->setSelectedTrack(this);

                    // Only handle click-to-seek if we're not about to start dragging
                    // (We'll determine this based on whether the mouse moves significantly)

                    if (event.getNumberOfClicks() == 2)
                    {
                        // Double-click: ALWAYS play this track from clicked position
                        spdlog::info("Double-click on track - requesting playback");
                        if (timeline->onPlaybackRequested)
                        {
                            auto localClick = event.position;
                            auto trackBounds = getBounds();
                            double clickTime = (trackBounds.getX() + localClick.x) / timeline->getPixelsPerSecond();

                            const auto startTime = std::chrono::duration<double>(m_mixTrack.mixStartTime).count();
                            double trackOffset = clickTime - startTime;
                            trackOffset = juce::jlimit(0.0, std::chrono::duration<double>(m_trackInfo.duration).count(), trackOffset);

                            juce::File audioFile(m_trackInfo.filepath.string());
                            timeline->onPlaybackRequested(audioFile, trackOffset);
                        }
                    }
                    // Single-click behavior will be handled in mouseUp if no drag occurred
                }
            }
        }


        void MixTrackComponent::resized()
        {
            auto bounds = getLocalBounds();
            // Place the label in the top section
            m_infoLabel.setBounds(bounds.removeFromTop(textSectionHeight).reduced(4, 0));

        }

        // In MixTrackComponent.cpp
        bool MixTrackComponent::isSelected() const
        {
            // Get parent timeline and check if we're the selected track
            if (auto *timeline = findParentComponentOfClass<TimelineComponent>())
            {
                return timeline->getSelectedTrack() == this;
            }
            return false;
        }

        void MixTrackComponent::paint(juce::Graphics &g)
        {
            auto &lf = getLookAndFeel();
            auto bounds = getLocalBounds();

            // Background color - different if selected
            juce::Colour bgColor = lf.findColour(juce::TextEditor::backgroundColourId);
            if (isSelected())
            {
                bgColor = bgColor.brighter(0.2f); // Slightly brighter when selected
            }

            g.setColour(bgColor);
            g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

            auto waveformArea = bounds.removeFromBottom(waveformSectionHeight);
            g.setColour(lf.findColour(juce::Slider::thumbColourId));

            // Selection border
            if (isSelected())
            {
                g.setColour(juce::Colours::orange); // Or theme color
                g.drawRoundedRectangle(bounds.toFloat().reduced(1), 4.0f, 2.0f);
            }

            m_thumbnail.drawChannel(g, waveformArea.reduced(2),
                                    0.0,                          // start time
                                    m_thumbnail.getTotalLength(), // end time
                                    0,                            // channel index to draw (0 = Left)
                                    1.0f);                        // vertical zoom

            // Draw volume envelope on top
            drawVolumeEnvelope(g, waveformArea);
        }

        void MixTrackComponent::drawVolumeEnvelope(juce::Graphics &g, juce::Rectangle<int> area)
        {
            if (m_mixTrack.envelopePoints.empty())
            {
                // No envelope data - just draw a flat line at full volume
                g.setColour(juce::Colours::yellow.withAlpha(0.8f));
                float fullVolumeY = area.getY() + (area.getHeight() * 0.2f); // 80% up from bottom
                g.drawHorizontalLine(juce::roundToInt(fullVolumeY), area.getX(), area.getRight());
                return;
            }

            juce::Path volumePath;
            const auto trackDuration = std::chrono::duration<double>(m_trackInfo.duration).count();

            // Start the path
            bool pathStarted = false;

            for (size_t i = 0; i < m_mixTrack.envelopePoints.size(); ++i)
            {
                const auto &point = m_mixTrack.envelopePoints[i];

                // Convert time to X position (relative to track start)
                const double timeInSeconds = std::chrono::duration<double>(point.time).count();
                const float x = area.getX() + (timeInSeconds / trackDuration) * area.getWidth();

                // Convert volume to Y position (0% = bottom, 100% = top)
                const float volumePercent = point.volume / float(database::VOLUME_NORMALIZATION);
                const float y = area.getBottom() - (volumePercent * area.getHeight());

                if (!pathStarted)
                {
                    volumePath.startNewSubPath(x, y);
                    pathStarted = true;
                }
                else
                {
                    volumePath.lineTo(x, y);
                }

                // Optional: Draw envelope point markers
                g.setColour(juce::Colours::orange);
                g.fillEllipse(x - 2, y - 2, 4, 4);
            }

            // Draw the envelope line
            g.setColour(juce::Colours::yellow.withAlpha(0.8f));
            g.strokePath(volumePath, juce::PathStrokeType(2.0f));

            // Debug logging for the envelope shape
            spdlog::debug("Track {}: {} envelope points, duration={:.1f}s", m_mixTrack.trackId, m_mixTrack.envelopePoints.size(), trackDuration);
        }

        void MixTrackComponent::changeListenerCallback(juce::ChangeBroadcaster *source)
        {
            if (source == &m_thumbnail)
            {
                repaint();
            }
        }

        void MixTrackComponent::setTopLeftPositionWithLogging(int newX, int newY)
        {
            auto oldPos = getPosition();
            spdlog::info("POSITION_CHANGE: Track {}, from ({},{}) to ({},{}), isDragging={}", m_mixTrack.trackId, oldPos.x, oldPos.y, newX, newY, m_isDragging);

            setTopLeftPosition(newX, newY);

            // Log the actual position after setting (to catch any constraints)
            auto newPos = getPosition();
            if (newPos.x != newX || newPos.y != newY)
            {
                spdlog::warn("POSITION_CONSTRAINED: Track {}, requested ({},{}), actual ({},{})", m_mixTrack.trackId, newX, newY, newPos.x, newPos.y);
            }
        }

        void MixTrackComponent::mouseDrag(const juce::MouseEvent &event)
        {
            if (event.mods.isLeftButtonDown())
            {
                if (!m_isDragging)
                {
                    m_isDragging = true;
                    m_originalTrackX = getX();

                    // Lock the constrainer to the current Y position
                    int currentY = getY();
                    m_constrainer.setLockedY(currentY);

                    spdlog::info("DRAG_START: Track {}, originalX={}, locking Y to {}", m_mixTrack.trackId, m_originalTrackX, currentY);

                    if (auto *timeline = findParentComponentOfClass<TimelineComponent>())
                    {
                        timeline->startTrackDrag(this);
                    }
                }

                // Use JUCE's ComponentDragger with our horizontal-only constrainer
                m_dragger.dragComponent(this, event, &m_constrainer);

                // Log the result
                spdlog::debug("DRAG_MOVE: Track {}, position=({},{})", m_mixTrack.trackId, getX(), getY());

                // Notify timeline of new position
                if (auto *timeline = findParentComponentOfClass<TimelineComponent>())
                {
                    double newTime = getX() / timeline->getPixelsPerSecond();
                    timeline->updateTrackDrag(this, newTime);
                }
            }
        }

        void MixTrackComponent::mouseUp(const juce::MouseEvent &event)
        {
            if (m_isDragging)
            {
                spdlog::info("Finished dragging track ID: {}", m_mixTrack.trackId);

                // Notify timeline that drag is complete
                if (auto *timeline = findParentComponentOfClass<TimelineComponent>())
                {
                    double finalTime = getX() / timeline->getPixelsPerSecond();
                    timeline->finishTrackDrag(this, finalTime);
                }

                m_isDragging = false;
            }
        }


    } // namespace ui
} // namespace jucyaudio