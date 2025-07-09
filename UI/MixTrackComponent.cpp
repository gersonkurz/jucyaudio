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
                // FIRST: Check for envelope point hits (highest priority)
                if (auto hitPointIndex = hitTestEnvelopePoint(event.position.toInt()))
                {
                    m_selectedEnvelopePointIndex = hitPointIndex;
                    m_isDraggingEnvelopePoint = true;
                    m_envelopePointDragStart = event.position.toInt();
                    m_originalEnvelopePoint = m_mixTrack.envelopePoints[*hitPointIndex];

                    // Ensure track is selected but don't start track dragging
                    if (auto *timeline = findParentComponentOfClass<TimelineComponent>())
                    {
                        timeline->setSelectedTrack(this);
                    }

                    repaint();
                    return; // Early exit - don't process track selection/dragging
                }

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
                    else if (event.getNumberOfClicks() == 1)
                    {
                        // **FIX: Initialize drag state here**
                        m_originalTrackX = getX();
                        int currentY = getY();
                        m_constrainer.setLockedY(currentY);

                        // Tell ComponentDragger where the drag started
                        m_dragger.startDraggingComponent(this, event);

                        // Single-click behavior will be handled in mouseUp if no drag occurred
                    }
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

            // Build the envelope path by connecting all points
            bool pathStarted = false;
            for (size_t i = 0; i < m_mixTrack.envelopePoints.size(); ++i)
            {
                const auto &point = m_mixTrack.envelopePoints[i];
                auto screenPos = envelopePointToScreenPosition(point);

                if (!pathStarted)
                {
                    volumePath.startNewSubPath(screenPos.x, screenPos.y);
                    pathStarted = true;
                }
                else
                {
                    volumePath.lineTo(screenPos.x, screenPos.y);
                }
            }

            // Draw the envelope line FIRST (so points appear on top)
            g.setColour(juce::Colours::yellow.withAlpha(0.8f));
            g.strokePath(volumePath, juce::PathStrokeType(2.0f));

            // Draw envelope points with different states ON TOP of the line
            for (size_t i = 0; i < m_mixTrack.envelopePoints.size(); ++i)
            {
                const auto &point = m_mixTrack.envelopePoints[i];
                auto screenPos = envelopePointToScreenPosition(point);

                // Choose color based on state
                juce::Colour pointColor = juce::Colours::orange;
                float pointSize = 4.0f;

                if (m_selectedEnvelopePointIndex == i)
                {
                    pointColor = juce::Colours::yellow;
                    pointSize = 6.0f;
                }
                else if (m_hoveredEnvelopePointIndex == i)
                {
                    pointColor = juce::Colours::orange.brighter();
                    pointSize = 5.0f;
                }

                g.setColour(pointColor);
                g.fillEllipse(screenPos.x - pointSize / 2, screenPos.y - pointSize / 2, pointSize, pointSize);
            }

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
            if (m_isDraggingEnvelopePoint && m_selectedEnvelopePointIndex.has_value())
            {
                auto newPoint = screenPositionToEnvelopePoint(event.position.toInt());
                constrainEnvelopePoint(*m_selectedEnvelopePointIndex, newPoint);

                // Update the envelope point
                const_cast<database::MixTrack &>(m_mixTrack).envelopePoints[*m_selectedEnvelopePointIndex] = newPoint;

                repaint();
                return;
            }

            if (event.mods.isLeftButtonDown() && !m_isDraggingEnvelopePoint)
            {
                if (!m_isDragging)
                {
                    m_isDragging = true;

                    spdlog::info("DRAG_START: Track {}, originalX={}, locking Y to {}", m_mixTrack.trackId, m_originalTrackX, getY());

                    if (auto *timeline = findParentComponentOfClass<TimelineComponent>())
                    {
                        timeline->startTrackDrag(this);
                    }
                }

                // Use JUCE's ComponentDragger - it now knows the original mouse position
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
            if (m_isDraggingEnvelopePoint)
            {
                // Notify of envelope change
                if (onEnvelopeChanged)
                {
                    onEnvelopeChanged(m_mixTrack.trackId, m_mixTrack.envelopePoints);
                }

                m_isDraggingEnvelopePoint = false;
                return;
            }

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

        void MixTrackComponent::mouseMove(const juce::MouseEvent &event)
        {
            auto hoveredPoint = hitTestEnvelopePoint(event.position.toInt());

            if (hoveredPoint != m_hoveredEnvelopePointIndex)
            {
                m_hoveredEnvelopePointIndex = hoveredPoint;
                repaint();
            }

            // Update cursor
            if (hoveredPoint.has_value())
            {
                setMouseCursor(juce::MouseCursor::PointingHandCursor);
            }
            else
            {
                setMouseCursor(juce::MouseCursor::NormalCursor);
            }
        }

        // Add these implementations to MixTrackComponent.cpp

        std::optional<size_t> MixTrackComponent::hitTestEnvelopePoint(juce::Point<int> mousePos) const
        {
            if (m_mixTrack.envelopePoints.empty())
                return std::nullopt;

            constexpr int HIT_RADIUS = 8; // Slightly larger than visual point for easier clicking

            auto bounds = getLocalBounds();
            auto waveformArea = bounds.removeFromBottom(waveformSectionHeight);

            for (size_t i = 0; i < m_mixTrack.envelopePoints.size(); ++i)
            {
                auto pointScreenPos = envelopePointToScreenPosition(m_mixTrack.envelopePoints[i]);

                // Only test points within the waveform area
                if (waveformArea.contains(pointScreenPos))
                {
                    if (mousePos.getDistanceFrom(pointScreenPos) <= HIT_RADIUS)
                    {
                        return i;
                    }
                }
            }

            return std::nullopt;
        }

        juce::Point<int> MixTrackComponent::envelopePointToScreenPosition(const database::EnvelopePoint &point) const
        {
            auto bounds = getLocalBounds();
            auto waveformArea = bounds.removeFromBottom(waveformSectionHeight);

            const auto trackDuration = std::chrono::duration<double>(m_trackInfo.duration).count();
            const double timeInSeconds = std::chrono::duration<double>(point.time).count();

            // Convert time to X position (relative to track start)
            const float x = waveformArea.getX() + (timeInSeconds / trackDuration) * waveformArea.getWidth();

            // Convert volume to Y position (0% = bottom, 100% = top)
            const float volumePercent = point.volume / float(database::VOLUME_NORMALIZATION);
            const float y = waveformArea.getBottom() - (volumePercent * waveformArea.getHeight());

            return juce::Point<int>(juce::roundToInt(x), juce::roundToInt(y));
        }

        database::EnvelopePoint MixTrackComponent::screenPositionToEnvelopePoint(juce::Point<int> screenPos) const
        {
            auto bounds = getLocalBounds();
            auto waveformArea = bounds.removeFromBottom(waveformSectionHeight);

            const auto trackDuration = std::chrono::duration<double>(m_trackInfo.duration).count();

            // Convert X position to time
            const float relativeX = (screenPos.x - waveformArea.getX()) / float(waveformArea.getWidth());
            const double timeInSeconds = juce::jlimit(0.0, trackDuration, relativeX * trackDuration);

            // Convert Y position to volume
            const float relativeY = (waveformArea.getBottom() - screenPos.y) / float(waveformArea.getHeight());
            const float volumePercent = juce::jlimit(0.0f, 1.0f, relativeY);

            database::EnvelopePoint result;
            result.time = std::chrono::milliseconds(static_cast<int64_t>(timeInSeconds * 1000));
            result.volume = static_cast<Volume_t>(volumePercent * database::VOLUME_NORMALIZATION);

            return result;
        }

        void MixTrackComponent::constrainEnvelopePoint(size_t pointIndex, database::EnvelopePoint &point) const
        {
            if (pointIndex >= m_mixTrack.envelopePoints.size())
                return;

            // Volume constraints (0% to 100%)
            point.volume = juce::jlimit(Volume_t(0), database::VOLUME_NORMALIZATION, point.volume);

            // Time constraints: maintain ordering between adjacent points
            if (pointIndex > 0)
            {
                const auto &prevPoint = m_mixTrack.envelopePoints[pointIndex - 1];
                point.time = std::max(point.time, prevPoint.time);
            }

            if (pointIndex < m_mixTrack.envelopePoints.size() - 1)
            {
                const auto &nextPoint = m_mixTrack.envelopePoints[pointIndex + 1];
                point.time = std::min(point.time, nextPoint.time);
            }

            // Ensure time is within track bounds
            const auto trackDuration = m_trackInfo.duration;
            point.time = std::min(point.time, trackDuration);
            point.time = std::max(point.time, std::chrono::milliseconds(0));
        }

    } // namespace ui
} // namespace jucyaudio