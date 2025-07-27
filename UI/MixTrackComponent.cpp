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
                // FIRST: Check for marker hits (highest priority)
                auto markerHit = hitTestMarker(event.position.toInt());
                if (markerHit != MarkerType::None)
                {
                    m_draggedMarker = markerHit;
                    m_originalMixTrack = m_mixTrack; // Save original state
                    
                    // Ensure track is selected
                    if (auto *timeline = findParentComponentOfClass<TimelineComponent>())
                    {
                        timeline->setSelectedTrack(this);
                    }
                    
                    repaint();
                    return; // Early exit
                }
                
                // SECOND: Check for envelope point hits
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
                    
                    // Calculate and set the click position in the timeline
                    auto localClick = event.position;
                    auto trackBounds = getBounds();
                    double clickTime = (trackBounds.getX() + localClick.x) / timeline->getPixelsPerSecond();
                    timeline->setCurrentTimePosition(clickTime);
                    
                    // Ensure timeline has keyboard focus for Space/Escape keys
                    timeline->grabKeyboardFocus();

                    // Only handle click-to-seek if we're not about to start dragging
                    // (We'll determine this based on whether the mouse moves significantly)

                    if (event.getNumberOfClicks() == 2)
                    {
                        // Double-click: Play the entire mix from clicked position
                        spdlog::info("Double-click on track - requesting mix playback");
                        auto localClick = event.position;
                        auto trackBounds = getBounds();
                        double clickTime = (trackBounds.getX() + localClick.x) / timeline->getPixelsPerSecond();
                        
                        // Use the always-play callback for double-clicks
                        if (timeline->onMixPlaybackAlwaysRequested)
                        {
                            timeline->onMixPlaybackAlwaysRequested(clickTime);
                        }
                        else
                        {
                            timeline->playMixFromPosition(clickTime);
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
            
            // Draw cue and attach point markers
            drawCueAndAttachMarkers(g, waveformArea);
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
            // Handle marker dragging
            if (m_draggedMarker != MarkerType::None)
            {
                updateMarkerPosition(m_draggedMarker, event.position.toInt().x);
                repaint();
                return;
            }
            
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
            // Handle marker drag completion
            if (m_draggedMarker != MarkerType::None)
            {
                // Notify of cue/attach change
                if (onCueAttachChanged)
                {
                    onCueAttachChanged(m_mixTrack.trackId, m_mixTrack);
                }
                
                m_draggedMarker = MarkerType::None;
                repaint();
                return;
            }
            
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
            // Check for marker hover
            auto hoveredMarker = hitTestMarker(event.position.toInt());
            bool needsRepaint = false;
            
            if (hoveredMarker != m_hoveredMarker)
            {
                m_hoveredMarker = hoveredMarker;
                needsRepaint = true;
            }
            
            // Check for envelope point hover
            auto hoveredPoint = hitTestEnvelopePoint(event.position.toInt());

            if (hoveredPoint != m_hoveredEnvelopePointIndex)
            {
                m_hoveredEnvelopePointIndex = hoveredPoint;
                needsRepaint = true;
            }
            
            if (needsRepaint)
            {
                repaint();
            }

            // Update cursor
            if (hoveredMarker != MarkerType::None || hoveredPoint.has_value())
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

            // Add a small margin so the last point isn't right at the edge
            const int margin = 5;
            const int usableWidth = waveformArea.getWidth() - (2 * margin);
            
            // Convert time to X position (relative to track start)
            const float x = waveformArea.getX() + margin + (timeInSeconds / trackDuration) * usableWidth;

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

            // Use the same margin as in envelopePointToScreenPosition
            const int margin = 5;
            const int usableWidth = waveformArea.getWidth() - (2 * margin);

            // Convert X position to time
            const float relativeX = (screenPos.x - waveformArea.getX() - margin) / float(usableWidth);
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
        
        void MixTrackComponent::drawCueAndAttachMarkers(juce::Graphics &g, juce::Rectangle<int> area)
        {
            const auto trackDuration = m_trackInfo.duration;
            
            // Helper lambda to draw a vertical marker
            auto drawMarker = [&](Duration_t time, juce::Colour colour, const char* label, bool isHovered)
            {
                if (time < Duration_t{0} || time > trackDuration)
                    return;
                    
                const double timeInSeconds = std::chrono::duration<double>(time).count();
                const double trackDurationSeconds = std::chrono::duration<double>(trackDuration).count();
                const float x = area.getX() + (timeInSeconds / trackDurationSeconds) * area.getWidth();
                
                // Draw vertical line
                g.setColour(isHovered ? colour.brighter(0.5f) : colour);
                g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
                
                // Draw handle at top
                const float handleSize = 8.0f;
                juce::Rectangle<float> handle(x - handleSize/2, area.getY() - handleSize/2, handleSize, handleSize);
                g.fillEllipse(handle);
                
                // Draw label
                g.setFont(10.0f);
                g.drawText(label, juce::roundToInt(x) + 2, area.getY() - 12, 50, 12, juce::Justification::left);
            };
            
            // Draw cue markers (green)
            drawMarker(m_mixTrack.cueStart, juce::Colours::green, "CS", m_hoveredMarker == MarkerType::CueStart);
            
            // For cueEnd, calculate actual position (0 means track end, negative is relative to end)
            Duration_t cueEndPos = m_mixTrack.cueEnd;
            if (cueEndPos == Duration_t{0})
            {
                cueEndPos = trackDuration;
            }
            else if (cueEndPos < Duration_t{0})
            {
                cueEndPos = trackDuration + cueEndPos;
            }
            drawMarker(cueEndPos, juce::Colours::red, "CE", m_hoveredMarker == MarkerType::CueEnd);
            
            // Draw attach markers (blue/purple)
            drawMarker(m_mixTrack.attachFrom, juce::Colours::blue, "AF", m_hoveredMarker == MarkerType::AttachFrom);
            drawMarker(m_mixTrack.attachTo, juce::Colours::purple, "AT", m_hoveredMarker == MarkerType::AttachTo);
        }
        
        MixTrackComponent::MarkerType MixTrackComponent::hitTestMarker(juce::Point<int> mousePos) const
        {
            auto bounds = getLocalBounds();
            auto waveformArea = bounds.removeFromBottom(waveformSectionHeight);
            
            // Only test within waveform area
            if (!waveformArea.contains(mousePos))
                return MarkerType::None;
                
            const int hitThreshold = 5; // pixels
            
            // Test each marker
            auto testMarker = [&](MarkerType type) -> bool
            {
                int markerX = getMarkerXPosition(type);
                return std::abs(mousePos.x - markerX) <= hitThreshold;
            };
            
            if (testMarker(MarkerType::CueStart)) return MarkerType::CueStart;
            if (testMarker(MarkerType::CueEnd)) return MarkerType::CueEnd;
            if (testMarker(MarkerType::AttachFrom)) return MarkerType::AttachFrom;
            if (testMarker(MarkerType::AttachTo)) return MarkerType::AttachTo;
            
            return MarkerType::None;
        }
        
        int MixTrackComponent::getMarkerXPosition(MarkerType marker) const
        {
            auto bounds = getLocalBounds();
            auto waveformArea = bounds.removeFromBottom(waveformSectionHeight);
            
            const auto trackDuration = m_trackInfo.duration;
            const double trackDurationSeconds = std::chrono::duration<double>(trackDuration).count();
            
            Duration_t markerTime{0};
            
            switch (marker)
            {
                case MarkerType::CueStart:
                    markerTime = m_mixTrack.cueStart;
                    break;
                    
                case MarkerType::CueEnd:
                    markerTime = m_mixTrack.cueEnd;
                    if (markerTime == Duration_t{0})
                        markerTime = trackDuration;
                    else if (markerTime < Duration_t{0})
                        markerTime = trackDuration + markerTime;
                    break;
                    
                case MarkerType::AttachFrom:
                    markerTime = m_mixTrack.attachFrom;
                    break;
                    
                case MarkerType::AttachTo:
                    markerTime = m_mixTrack.attachTo;
                    break;
                    
                default:
                    return 0;
            }
            
            const double timeInSeconds = std::chrono::duration<double>(markerTime).count();
            return waveformArea.getX() + juce::roundToInt((timeInSeconds / trackDurationSeconds) * waveformArea.getWidth());
        }
        
        Duration_t MixTrackComponent::screenXToTrackTime(int screenX) const
        {
            auto bounds = getLocalBounds();
            auto waveformArea = bounds.removeFromBottom(waveformSectionHeight);
            
            const auto trackDuration = m_trackInfo.duration;
            const double trackDurationSeconds = std::chrono::duration<double>(trackDuration).count();
            
            const float relativeX = (screenX - waveformArea.getX()) / float(waveformArea.getWidth());
            const double timeInSeconds = juce::jlimit(0.0, trackDurationSeconds, relativeX * trackDurationSeconds);
            
            return std::chrono::milliseconds(static_cast<int64_t>(timeInSeconds * 1000));
        }
        
        void MixTrackComponent::updateMarkerPosition(MarkerType marker, int newX)
        {
            Duration_t newTime = screenXToTrackTime(newX);
            const auto trackDuration = m_trackInfo.duration;
            
            // Apply constraints based on marker type
            switch (marker)
            {
                case MarkerType::CueStart:
                    // Constrain: 0 <= cueStart < cueEnd (or track end if cueEnd is 0)
                    newTime = std::max(Duration_t{0}, newTime);
                    if (m_mixTrack.cueEnd > Duration_t{0})
                    {
                        newTime = std::min(newTime, m_mixTrack.cueEnd - Duration_t{1}); // At least 1ms before end
                    }
                    else
                    {
                        newTime = std::min(newTime, trackDuration - Duration_t{1});
                    }
                    const_cast<database::MixTrack&>(m_mixTrack).cueStart = newTime;
                    break;
                    
                case MarkerType::CueEnd:
                    // Convert to relative format for storage
                    if (newTime >= trackDuration)
                    {
                        const_cast<database::MixTrack&>(m_mixTrack).cueEnd = Duration_t{0}; // 0 means track end
                    }
                    else
                    {
                        // Ensure cueEnd > cueStart
                        newTime = std::max(newTime, m_mixTrack.cueStart + Duration_t{1});
                        const_cast<database::MixTrack&>(m_mixTrack).cueEnd = newTime;
                    }
                    break;
                    
                case MarkerType::AttachFrom:
                    // Constrain: 0 <= attachFrom < attachTo
                    newTime = std::max(Duration_t{0}, newTime);
                    newTime = std::min(newTime, m_mixTrack.attachTo - Duration_t{1});
                    const_cast<database::MixTrack&>(m_mixTrack).attachFrom = newTime;
                    break;
                    
                case MarkerType::AttachTo:
                    // Constrain: attachFrom < attachTo <= duration
                    newTime = std::max(m_mixTrack.attachFrom + Duration_t{1}, newTime);
                    newTime = std::min(newTime, trackDuration);
                    const_cast<database::MixTrack&>(m_mixTrack).attachTo = newTime;
                    break;
                    
                default:
                    break;
            }
        }

    } // namespace ui
} // namespace jucyaudio