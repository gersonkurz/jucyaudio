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
            // Setup the info label with track duration
            const auto durationSeconds = std::chrono::duration_cast<std::chrono::seconds>(trackInfo.duration).count();
            const int minutes = durationSeconds / 60;
            const int seconds = durationSeconds % 60;
            juce::String durationText = juce::String::formatted("%d:%02d", minutes, seconds);

            juce::String infoText = juce::String(trackInfo.title) + " (" + durationText + ")";
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
                    // Special handling for cue-end dragging
                    if (markerHit == MarkerType::CueEnd)
                    {
                        m_cueEndDragState.isDragging = true;
                        m_cueEndDragState.draggedMarker = MarkerType::CueEnd;
                        
                        // Cache coordinate system
                        auto bounds = getLocalBounds();
                        m_cueEndDragState.cachedWaveformBounds = bounds.removeFromBottom(waveformSectionHeight);
                        
                        const double trackDurationSeconds = std::chrono::duration<double>(m_trackInfo.duration).count();
                        m_cueEndDragState.cachedPixelsPerSecond = 
                            m_cueEndDragState.cachedWaveformBounds.getWidth() / trackDurationSeconds;
                        
                        // Store original cue end position
                        m_cueEndDragState.originalMarkerTime = m_mixTrack.cueEnd;
                        if (m_cueEndDragState.originalMarkerTime == Duration_t{0})
                        {
                            m_cueEndDragState.originalMarkerTime = m_trackInfo.duration;
                        }
                        else if (m_cueEndDragState.originalMarkerTime < Duration_t{0})
                        {
                            m_cueEndDragState.originalMarkerTime = m_trackInfo.duration + m_cueEndDragState.originalMarkerTime;
                        }
                        
                        m_cueEndDragState.previewMarkerTime = m_cueEndDragState.originalMarkerTime;
                        m_cueEndDragState.maxVisualExtension = trackDurationSeconds * 1.5;
                    }
                    else
                    {
                        // Original behavior for other markers
                        m_draggedMarker = markerHit;
                        m_originalMixTrack = m_mixTrack; // Save original state
                    }
                    
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

            g.setColour(
                isSelected() ? lf.findColour(juce::TextEditor::backgroundColourId).brighter(0.2f) : lf.findColour(juce::TextEditor::backgroundColourId));
            g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

            auto waveformArea = bounds.removeFromBottom(waveformSectionHeight);

            if (isSelected())
            {
                g.setColour(juce::Colours::orange);
                g.drawRoundedRectangle(bounds.toFloat().reduced(1), 4.0f, 2.0f);
            }

            // --- FULLY DYNAMIC THREE-PART DRAWING LOGIC ---

            const auto trackDuration = m_trackInfo.duration;
            // Now using the helper method we just added.
            const auto cueEndActual = m_mixTrack.getCueEndActual(trackDuration);

            // 1. Calculate durations for the three parts dynamically.
            const double silenceAtStartSeconds = (m_mixTrack.cueStart < Duration_t{0}) ? std::chrono::duration<double>(-m_mixTrack.cueStart).count() : 0.0;

            const double silenceAtEndSeconds = (cueEndActual > trackDuration) ? std::chrono::duration<double>(cueEndActual - trackDuration).count() : 0.0;

            const double totalVisibleDurationSecs = std::chrono::duration<double>(m_mixTrack.getEffectiveDuration(trackDuration)).count();

            if (totalVisibleDurationSecs <= 0.0)
                return;

            const double waveformDurationOnScreen = totalVisibleDurationSecs - silenceAtStartSeconds - silenceAtEndSeconds;

            // 2. Calculate the sub-rectangles based on the dynamic proportions.
            const double silenceBeforeProportion = silenceAtStartSeconds / totalVisibleDurationSecs;
            const double waveformProportion = waveformDurationOnScreen / totalVisibleDurationSecs;

            const int silenceBeforeWidth = juce::roundToInt((float)waveformArea.getWidth() * (float)silenceBeforeProportion);
            const int waveformDrawWidth = juce::roundToInt((float)waveformArea.getWidth() * (float)waveformProportion);

            auto waveformDrawRect = waveformArea.withX(waveformArea.getX() + silenceBeforeWidth).withWidth(waveformDrawWidth);

            // 3. Determine which part of the source audio to draw.
            const double thumbnailStartTime = std::chrono::duration<double>(std::max(Duration_t{0}, m_mixTrack.cueStart)).count();
            const double thumbnailEndTime = std::chrono::duration<double>(std::min(trackDuration, cueEndActual)).count();

            // 4. Draw the required part of the thumbnail into its specific sub-rectangle.
            if (waveformDrawRect.getWidth() > 0)
            {
                g.setColour(lf.findColour(juce::Slider::thumbColourId));
                m_thumbnail.drawChannel(g,
                    waveformDrawRect.reduced(2),
                    thumbnailStartTime, // Start time in source file
                    thumbnailEndTime,   // End time in source file
                    0,
                    1.0f);

                // 5. Draw overlays relative to the waveform's specific drawing area.
                drawNonAudibleRegions(g, waveformDrawRect);
                drawVolumeEnvelope(g, waveformDrawRect);
                drawCueAndAttachMarkers(g, waveformDrawRect);
            }
        }

        void MixTrackComponent::drawVolumeEnvelope(juce::Graphics &g, const juce::Rectangle<int> &area)
        {
            // If there are no envelope points, there is nothing to draw.
            if (m_mixTrack.envelopePoints.empty())
            {
                return;
            }

            // --- Coordinate Calculation Helper ---
            // This lambda is a clean way to ensure we use the exact same logic for both
            // the path and the individual points, all relative to the provided 'area'.
            auto pointToScreen = [&](const database::EnvelopePoint &point) -> juce::Point<float>
            {
                const double timeInSeconds = std::chrono::duration<double>(point.time).count();
                const double trackDurationSeconds = std::chrono::duration<double>(m_trackInfo.duration).count();

                // Prevent division by zero if track has no duration
                if (trackDurationSeconds <= 0)
                {
                    return {(float)area.getX(), (float)area.getBottom()};
                }

                // X position is proportional to the point's time relative to the track's total duration,
                // scaled to the provided 'area' width.
                const float x = (float)area.getX() + (float)(timeInSeconds / trackDurationSeconds) * (float)area.getWidth();

                // Y position is the volume percentage scaled to the 'area' height.
                const float y = (float)area.getBottom() - ((float)point.volume / (float)database::VOLUME_NORMALIZATION) * (float)area.getHeight();

                return {x, y};
            };

            // --- 1. Draw the Envelope Line ---
            juce::Path volumePath;
            for (size_t i = 0; i < m_mixTrack.envelopePoints.size(); ++i)
            {
                const auto &point = m_mixTrack.envelopePoints[i];
                const auto screenPos = pointToScreen(point);

                if (i == 0)
                {
                    volumePath.startNewSubPath(screenPos);
                }
                else
                {
                    volumePath.lineTo(screenPos);
                }
            }

            g.setColour(juce::Colours::yellow.withAlpha(0.8f));
            g.strokePath(volumePath, juce::PathStrokeType(2.0f));

            // --- 2. Draw the Interactive Points on Top of the Line ---
            for (size_t i = 0; i < m_mixTrack.envelopePoints.size(); ++i)
            {
                const auto &point = m_mixTrack.envelopePoints[i];
                const auto screenPos = pointToScreen(point);

                // Determine color and size based on selection or hover state
                juce::Colour pointColor = juce::Colours::orange;
                float pointSize = 4.0f;

                if (m_selectedEnvelopePointIndex.has_value() && m_selectedEnvelopePointIndex.value() == i)
                {
                    pointColor = juce::Colours::yellow;
                    pointSize = 6.0f;
                }
                else if (m_hoveredEnvelopePointIndex.has_value() && m_hoveredEnvelopePointIndex.value() == i)
                {
                    pointColor = juce::Colours::orange.brighter();
                    pointSize = 5.0f;
                }

                g.setColour(pointColor);
                g.fillEllipse(screenPos.x - pointSize / 2.0f, screenPos.y - pointSize / 2.0f, pointSize, pointSize);
            }
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
            // Handle cue-end dragging with cached coordinates
            if (m_cueEndDragState.isDragging)
            {
                // Use CACHED coordinates, not live ones
                const int deltaX = event.position.x - event.mouseDownPosition.x;
                const double deltaSeconds = deltaX / m_cueEndDragState.cachedPixelsPerSecond;
                
                // Calculate new preview position
                const double originalSeconds = std::chrono::duration<double>(m_cueEndDragState.originalMarkerTime).count();
                double newSeconds = std::max(0.0, originalSeconds + deltaSeconds);
                
                // Limit to max visual extension
                newSeconds = std::min(newSeconds, m_cueEndDragState.maxVisualExtension);
                
                m_cueEndDragState.previewMarkerTime = Duration_t(
                    std::chrono::milliseconds(static_cast<int64_t>(newSeconds * 1000)));
                
                repaint(); // Only repaint this component, not timeline
                return;
            }
            
            // Handle other marker dragging
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
            // Handle cue-end drag completion
            if (m_cueEndDragState.isDragging)
            {
                // Apply the preview to the actual model
                database::MixTrack updatedTrack = m_mixTrack;
                
                // Convert back to the stored format (0 means track end)
                const auto trackDuration = m_trackInfo.duration;
                if (m_cueEndDragState.previewMarkerTime >= trackDuration)
                {
                    // Beyond track duration - store as positive value
                    updatedTrack.cueEnd = m_cueEndDragState.previewMarkerTime;
                }
                else if (m_cueEndDragState.previewMarkerTime == trackDuration)
                {
                    // Exactly at track end - store as 0
                    updatedTrack.cueEnd = Duration_t{0};
                }
                else
                {
                    // Before track end - store as negative offset from end
                    updatedTrack.cueEnd = m_cueEndDragState.previewMarkerTime - trackDuration;
                }
                
                if (onCueAttachChanged)
                {
                    onCueAttachChanged(m_trackInfo.trackId, updatedTrack);
                }
                
                // Clear drag state
                m_cueEndDragState = DragState{};
                repaint();
                return;
            }
            
            // Handle other marker drag completion
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
            
            // For extended tracks, we need to adjust the test positions
            const auto effectiveDuration = m_mixTrack.getEffectiveDuration(m_trackInfo.duration);
            const double effectiveDurationSeconds = std::chrono::duration<double>(effectiveDuration).count();
            const double trackDurationSeconds = std::chrono::duration<double>(m_trackInfo.duration).count();

            for (size_t i = 0; i < m_mixTrack.envelopePoints.size(); ++i)
            {
                auto pointScreenPos = envelopePointToScreenPosition(m_mixTrack.envelopePoints[i]);
                
                // Adjust screen position for extended tracks
                if (effectiveDuration > m_trackInfo.duration)
                {
                    const float relativeX = (pointScreenPos.x - waveformArea.getX()) / float(waveformArea.getWidth());
                    const float waveformProportion = trackDurationSeconds / effectiveDurationSeconds;
                    pointScreenPos.x = waveformArea.getX() + juce::roundToInt(relativeX * waveformArea.getWidth() * (effectiveDurationSeconds / trackDurationSeconds));
                }

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

            const auto trackDuration = m_trackInfo.duration;
            const double trackDurationSeconds = std::chrono::duration<double>(trackDuration).count();
            const double timeInSeconds = std::chrono::duration<double>(point.time).count();

            // Add a small margin so the last point isn't right at the edge
            const int margin = 5;
            const int usableWidth = waveformArea.getWidth() - (2 * margin);
            
            // Convert time to X position (relative to track start)
            const float x = waveformArea.getX() + margin + (timeInSeconds / trackDurationSeconds) * usableWidth;

            // Convert volume to Y position (0% = bottom, 100% = top)
            const float volumePercent = point.volume / float(database::VOLUME_NORMALIZATION);
            const float y = waveformArea.getBottom() - (volumePercent * waveformArea.getHeight());

            return juce::Point<int>(juce::roundToInt(x), juce::roundToInt(y));
        }

        database::EnvelopePoint MixTrackComponent::screenPositionToEnvelopePoint(juce::Point<int> screenPos) const
        {
            auto bounds = getLocalBounds();
            auto waveformArea = bounds.removeFromBottom(waveformSectionHeight);

            const auto trackDuration = m_trackInfo.duration;
            const double trackDurationSeconds = std::chrono::duration<double>(trackDuration).count();

            // Use the same margin as in envelopePointToScreenPosition
            const int margin = 5;
            const int usableWidth = waveformArea.getWidth() - (2 * margin);

            // Convert X position to time
            const float relativeX = (screenPos.x - waveformArea.getX() - margin) / float(usableWidth);
            const double timeInSeconds = juce::jlimit(0.0, trackDurationSeconds, relativeX * trackDurationSeconds);

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

            // Ensure time is within track duration
            const auto trackDuration = m_trackInfo.duration;
            point.time = std::min(point.time, trackDuration);
            point.time = std::max(point.time, std::chrono::milliseconds(0));
        }
        
        void MixTrackComponent::drawNonAudibleRegions(juce::Graphics &g, juce::Rectangle<int> area)
        {
            // Skip if we're in cue-end drag mode - the drag preview handles visualization
            if (m_cueEndDragState.isDragging)
                return;
                
            const auto trackDuration = m_trackInfo.duration;
            const double trackDurationSeconds = std::chrono::duration<double>(trackDuration).count();
            
            // Calculate cue positions
            const double cueStartSeconds = std::chrono::duration<double>(m_mixTrack.cueStart).count();
            Duration_t cueEndPos = m_mixTrack.cueEnd;
            if (cueEndPos == Duration_t{0})
            {
                cueEndPos = trackDuration;
            }
            else if (cueEndPos < Duration_t{0})
            {
                cueEndPos = trackDuration + cueEndPos;
            }
            const double cueEndSeconds = std::chrono::duration<double>(cueEndPos).count();
            
            // Use getMarkerXPosition for consistent positioning
            const int cueStartX = getMarkerXPosition(MarkerType::CueStart);
            const int cueEndX = getMarkerXPosition(MarkerType::CueEnd);
            
            // Draw semi-transparent overlay for non-audible regions
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            
            // Before cue start
            if (cueStartX > area.getX())
            {
                g.fillRect(area.getX(), area.getY(), 
                          cueStartX - area.getX(), area.getHeight());
            }
            
            // After cue end (only if within track bounds)
            if (cueEndPos <= trackDuration)
            {
                // Calculate where the track waveform ends
                const auto effectiveDuration = m_mixTrack.getEffectiveDuration(trackDuration);
                if (effectiveDuration > trackDuration)
                {
                    // Extended track - waveform ends before component bounds
                    const double effectiveDurationSeconds = std::chrono::duration<double>(effectiveDuration).count();
                    const float waveformProportion = trackDurationSeconds / effectiveDurationSeconds;
                    const int waveformEndX = area.getX() + juce::roundToInt(area.getWidth() * waveformProportion);
                    
                    if (cueEndX < waveformEndX)
                    {
                        g.fillRect(cueEndX, area.getY(), 
                                  waveformEndX - cueEndX, area.getHeight());
                    }
                }
                else
                {
                    // Normal track - waveform fills component
                    if (cueEndX < area.getRight())
                    {
                        g.fillRect(cueEndX, area.getY(), 
                                  area.getRight() - cueEndX, area.getHeight());
                    }
                }
            }
        }
        
        void MixTrackComponent::drawCueAndAttachMarkers(juce::Graphics &g, juce::Rectangle<int> area)
        {
            // Draw each marker using getMarkerXPosition for consistent positioning
            auto drawMarker = [&](MarkerType type, juce::Colour colour, const char* label)
            {
                const int x = getMarkerXPosition(type);
                const bool isHovered = (m_hoveredMarker == type);
                
                // Only draw if within bounds
                if (x >= area.getX() && x <= area.getRight())
                {
                    // Draw vertical line
                    g.setColour(isHovered ? colour.brighter(0.5f) : colour);
                    g.drawVerticalLine(x, area.getY(), area.getBottom());
                    
                    // Draw handle at top
                    const float handleSize = 8.0f;
                    juce::Rectangle<float> handle(x - handleSize/2, area.getY() - handleSize/2, handleSize, handleSize);
                    g.fillEllipse(handle);
                    
                    // Draw label
                    if (label[0] != '\0') // Only draw label if not empty
                    {
                        g.setFont(10.0f);
                        g.drawText(label, x + 2, area.getY() - 12, 60, 12, juce::Justification::left);
                    }
                }
            };
            
            // Draw cue markers - these define the audible portion of the track
            const auto cueColor = juce::Colours::cyan;
            drawMarker(MarkerType::CueStart, cueColor, "Cue In");
            drawMarker(MarkerType::CueEnd, cueColor, "Cue Out");
            
            // Draw attach markers - these define where crossfades align
            // Keep them subtle since the main attach line is drawn at timeline level
            const auto attachColor = juce::Colours::orange.withAlpha(0.5f);
            drawMarker(MarkerType::AttachFrom, attachColor, "");
            drawMarker(MarkerType::AttachTo, attachColor, "");
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
            const auto effectiveDuration = m_mixTrack.getEffectiveDuration(trackDuration);
            const double effectiveDurationSeconds = std::chrono::duration<double>(effectiveDuration).count();
            
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
            
            // If the track is extended and this is the cue-end marker beyond track duration,
            // scale it to fit within the component bounds
            if (effectiveDuration > trackDuration && markerTime > trackDuration && marker == MarkerType::CueEnd)
            {
                // Map the extended portion to the available space
                const double extendedProportion = (timeInSeconds - trackDurationSeconds) / (effectiveDurationSeconds - trackDurationSeconds);
                const int trackEndX = waveformArea.getX() + juce::roundToInt((trackDurationSeconds / effectiveDurationSeconds) * waveformArea.getWidth());
                const int remainingWidth = waveformArea.getRight() - trackEndX;
                return trackEndX + juce::roundToInt(extendedProportion * remainingWidth);
            }
            else
            {
                // Normal case - position based on track duration
                return waveformArea.getX() + juce::roundToInt((timeInSeconds / trackDurationSeconds) * waveformArea.getWidth());
            }
        }
        
        Duration_t MixTrackComponent::screenXToTrackTime(int screenX) const
        {
            auto bounds = getLocalBounds();
            auto waveformArea = bounds.removeFromBottom(waveformSectionHeight);
            
            const auto trackDuration = m_trackInfo.duration;
            const double trackDurationSeconds = std::chrono::duration<double>(trackDuration).count();
            const auto effectiveDuration = m_mixTrack.getEffectiveDuration(trackDuration);
            const double effectiveDurationSeconds = std::chrono::duration<double>(effectiveDuration).count();
            
            // If track is extended, we need to handle the coordinate mapping differently
            if (effectiveDuration > trackDuration)
            {
                // Calculate where the track end would be visually
                const int trackEndX = waveformArea.getX() + juce::roundToInt((trackDurationSeconds / effectiveDurationSeconds) * waveformArea.getWidth());
                
                if (screenX <= trackEndX)
                {
                    // Within the track duration - normal mapping
                    const float relativeX = (screenX - waveformArea.getX()) / float(trackEndX - waveformArea.getX());
                    const double timeInSeconds = relativeX * trackDurationSeconds;
                    return std::chrono::milliseconds(static_cast<int64_t>(timeInSeconds * 1000));
                }
                else
                {
                    // Beyond track duration - in the extended area
                    const float relativeX = (screenX - trackEndX) / float(waveformArea.getRight() - trackEndX);
                    const double extraSeconds = relativeX * (effectiveDurationSeconds - trackDurationSeconds);
                    const double timeInSeconds = trackDurationSeconds + extraSeconds;
                    return std::chrono::milliseconds(static_cast<int64_t>(timeInSeconds * 1000));
                }
            }
            else
            {
                // Normal case - no extension
                const float relativeX = (screenX - waveformArea.getX()) / float(waveformArea.getWidth());
                const double timeInSeconds = relativeX * trackDurationSeconds;
                return std::chrono::milliseconds(static_cast<int64_t>(timeInSeconds * 1000));
            }
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
                    // CueEnd is now handled by the special drag state
                    // This case should not be reached
                    jassertfalse;
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