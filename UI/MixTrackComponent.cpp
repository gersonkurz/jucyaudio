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
        }

        MixTrackComponent::~MixTrackComponent()
        {
            m_thumbnail.removeChangeListener(this);
        }
        
        void MixTrackComponent::paint(juce::Graphics &g)
        {
            // --- 1. Basic Setup & Background ---
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

            // --- 2. The "Three-Part Model" Calculation ---
            const auto trackDuration = m_trackInfo.duration;
            const auto cueEndActual = m_mixTrack.getCueEndActual(trackDuration);
            const auto totalEffectiveDuration = m_mixTrack.getEffectiveDuration(trackDuration);
            const double totalVisibleDurationSecs = std::chrono::duration<double>(totalEffectiveDuration).count();

            // Safety check: Do not proceed if there is nothing to draw.
            if (totalVisibleDurationSecs <= 0.0)
                return;

            const double silenceAtStartSeconds = (m_mixTrack.cueStart < Duration_t{0}) ? std::chrono::duration<double>(-m_mixTrack.cueStart).count() : 0.0;
            const double silenceAtEndSeconds = (cueEndActual > trackDuration) ? std::chrono::duration<double>(cueEndActual - trackDuration).count() : 0.0;
            const double waveformDurationOnScreen = totalVisibleDurationSecs - silenceAtStartSeconds - silenceAtEndSeconds;

            // --- 3. Sub-Rectangle Calculation ---
            const double silenceBeforeProportion = silenceAtStartSeconds / totalVisibleDurationSecs;
            const double waveformProportion = waveformDurationOnScreen / totalVisibleDurationSecs;

            const int silenceBeforeWidth = juce::roundToInt((float)waveformArea.getWidth() * (float)silenceBeforeProportion);
            const int waveformDrawWidth = juce::roundToInt((float)waveformArea.getWidth() * (float)waveformProportion);

            auto waveformDrawRect = waveformArea.withX(waveformArea.getX() + silenceBeforeWidth).withWidth(waveformDrawWidth);

            // --- 4. Source Audio Range Calculation ---
            const double thumbnailStartTime = std::chrono::duration<double>(std::max(Duration_t{0}, m_mixTrack.cueStart)).count();
            const double thumbnailEndTime = std::chrono::duration<double>(std::min(trackDuration, cueEndActual)).count();

            // --- 5. Drawing ---
            if (waveformDrawRect.getWidth() > 0)
            {
                g.setColour(lf.findColour(juce::Slider::thumbColourId));
                // Draw the waveform from the source file into its designated sub-rectangle.
                m_thumbnail.drawChannel(g,
                    waveformDrawRect.reduced(2),
                    thumbnailStartTime,
                    thumbnailEndTime,
                    0, // Drawing channel 0 (left channel)
                    1.0f);

                // Draw overlays tied to the audio content relative to the waveform's rectangle.
                drawVolumeEnvelope(g, waveformArea);

                // Draw markers relative to the full area, as they can exist in silence.
                drawCueAndAttachMarkers(g, waveformArea);
            }
            else
            {
                // Still draw markers even if there is no waveform visible.
                drawCueAndAttachMarkers(g, waveformArea);
            }
        }
        
        void MixTrackComponent::resized()
        {
            auto bounds = getLocalBounds();
            // Place the label in the top section
            m_infoLabel.setBounds(bounds.removeFromTop(textSectionHeight).reduced(4, 0));
        }

        bool MixTrackComponent::isSelected() const
        {
            // Get parent timeline and check if we're the selected track
            if (const auto *timeline = findParentComponentOfClass<TimelineComponent>())
            {
                return timeline->getSelectedTrack() == this;
            }
            return false;
        }

       
        void MixTrackComponent::drawVolumeEnvelope(juce::Graphics &g, const juce::Rectangle<int> &area)
        {
            if (m_mixTrack.envelopePoints.empty())
            {
                return;
            }

            // --- Correct Coordinate Calculation Helper ---
            // This lambda now correctly maps an envelope point's time to a screen coordinate
            // within the component's full effective duration.
            auto pointToScreen = [&](const database::EnvelopePoint &point) -> juce::Point<float>
            {
                // Get the total visible duration of this component, including any silence.
                const auto effectiveDuration = m_mixTrack.getEffectiveDuration(m_trackInfo.duration);
                if (effectiveDuration <= Duration_t{0})
                {
                    return {(float)area.getX(), (float)area.getBottom()};
                }

                // To find the point's relative position, we compare its time to the component's start time (cueStart).
                const auto timeRelativeToComponentStart = point.time - m_mixTrack.cueStart;

                // Calculate the proportion of this relative time to the total visible duration.
                // This correctly handles all cases: trimming, extending, and negative time points.
                const double proportion =
                    std::chrono::duration<double>(timeRelativeToComponentStart).count() / std::chrono::duration<double>(effectiveDuration).count();

                // Apply that proportion to the component's full width to get the final X coordinate.
                const float x = (float)area.getX() + (float)proportion * (float)area.getWidth();

                // Y position is the volume percentage scaled to the 'area' height (this remains correct).
                const float y = (float)area.getBottom() - ((float)point.volume / (float)database::VOLUME_NORMALIZATION) * (float)area.getHeight();

                return {x, y};
            };

            // --- Drawing Logic (This part remains the same) ---

            // 1. Draw the Envelope Line
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
            // 1. Resolve the marker type to an absolute time value using our robust helpers.
            Duration_t markerTime{0};
            switch (marker)
            {
            case MarkerType::CueStart:
                markerTime = m_mixTrack.cueStart;
                break;
            case MarkerType::CueEnd:
                markerTime = m_mixTrack.getCueEndActual(m_trackInfo.duration);
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

            // 2. Map this absolute time to a pixel coordinate.
            const auto effectiveDuration = m_mixTrack.getEffectiveDuration(m_trackInfo.duration);
            if (effectiveDuration <= Duration_t{0})
            {
                return getLocalBounds().getX();
            }

            // Calculate the marker's time relative to the component's visible start time (which is cueStart).
            const auto relativeTime = markerTime - m_mixTrack.cueStart;

            // Find the proportion of this relative time to the total visible duration.
            const double proportion = std::chrono::duration<double>(relativeTime).count() / std::chrono::duration<double>(effectiveDuration).count();

            // Apply that proportion to the component's full width to get the final X coordinate.
            return getLocalBounds().getX() + juce::roundToInt(proportion * getWidth());

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

        
        Duration_t MixTrackComponent::xToTime(int x) const
        {
            const auto effectiveDuration = m_mixTrack.getEffectiveDuration(m_trackInfo.duration);
            if (getWidth() <= 0)
                return Duration_t{0};

            // Calculate the proportional position of the mouse click within the component's total width.
            const double proportion = (double)(x - getLocalBounds().getX()) / (double)getWidth();

            // Apply this proportion to the total effective duration to get the time offset.
            const auto timeOffset = std::chrono::duration<double>(proportion * std::chrono::duration<double>(effectiveDuration).count());

            // The absolute time is the component's start time (which is cueStart) plus the calculated offset.
            return m_mixTrack.cueStart + std::chrono::duration_cast<Duration_t>(timeOffset);
        }


        // Mouse Event Handlers --------------------------------------------------------
        
        void MixTrackComponent::mouseDown(const juce::MouseEvent &event)
        {
            if (event.mods.isLeftButtonDown())
            {
                // FIRST: Check for marker hits (highest priority)
                auto markerHit = hitTestMarker(event.position.toInt());
                if (markerHit != MarkerType::None)
                {
                    // --- THIS IS THE CORRECT, GENERIC LOGIC ---
                    m_draggedMarker = markerHit;
                    m_originalMixTrack = m_mixTrack;

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
                        // Single-click logic for seeking in the timeline is handled by the parent.
                        // We no longer initiate a component-wide drag here, as track positioning
                        // is strictly controlled by the attach-point model.
                    }
                }
            }
        }

        void MixTrackComponent::mouseDrag(const juce::MouseEvent &event)
        {
            // Handle other marker dragging
            if (m_draggedMarker != MarkerType::None)
            {
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
        
        void MixTrackComponent::mouseUp(const juce::MouseEvent &event)
        {
            if (m_draggedMarker == MarkerType::CueEnd)
            {
                // 1. Calculate the new absolute time based on the mouse release position.
                Duration_t newAbsoluteTime = xToTime(event.position.x);

                // 2. Convert this absolute time to our storage format (offset from track end).
                database::MixTrack updatedTrack = m_mixTrack;
                updatedTrack.cueEnd = newAbsoluteTime - m_trackInfo.duration;

                // 3. Fire the callback to update the data model and trigger a layout refresh.
                if (onCueAttachChanged)
                {
                    onCueAttachChanged(m_trackInfo.trackId, updatedTrack);
                }

                // 4. Reset the drag state.
                m_draggedMarker = MarkerType::None;
                repaint(); // Repaint to show the new marker position after the layout has changed.
                return;    // Early exit
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

    } // namespace ui
} // namespace jucyaudio