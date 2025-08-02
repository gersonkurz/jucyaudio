#include "BinaryData.h"
#include <Database/Includes/Constants.h>
#include <UI/MixTrackComponent.h>
#include <UI/TimelineComponent.h>
#include <Utils/AssortedUtils.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace
    {
        const auto ENVELOPE_PATH_LINE_THINKESS = 4.0f;

        // regular points are 10 pixels, active points are 20 pixels (visibly bigger)
        const auto ENVELOPE_POINT_STANDARD_RADIUS = 10.0f;
        const auto ENVELOPE_POINT_ACTIVE_RADIUS = 20.0f;

    }

    namespace ui
    {
        using namespace database;

        MixTrackComponent::MixTrackComponent(MixTrack &mixTrack, const TrackInfo &trackInfo, juce::AudioFormatManager &formatManager,
                                             juce::AudioThumbnailCache &thumbnailCache)
            : m_mixTrack{mixTrack},
              m_trackInfo{trackInfo},
              m_originalEnvelopePoint{},
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
            }

            // Draw markers relative to the full area, as they can exist in silence.
            drawAttachMarkers(g, waveformArea);
        }
        

        void MixTrackComponent::drawVolumeEnvelope(juce::Graphics &g, const juce::Rectangle<int> &area)
        {
            // if there are no points, nothing to draw
            if (m_mixTrack.envelopePoints.empty())
            {
                return;
            }

            // --- 1. Draw the Envelope Line ---
            juce::Path volumePath;
            bool firstPoint = true;
            for (const auto &point : m_mixTrack.envelopePoints)
            {
                const auto screenPos = envelopePointToScreenPosition(point);
                if (firstPoint)
                {
                    volumePath.startNewSubPath(screenPos);
                    firstPoint = false;
                }
                else
                {
                    volumePath.lineTo(screenPos);
                }
            }

            g.setColour(juce::Colours::yellow.withAlpha(0.8f));
            g.strokePath(volumePath, juce::PathStrokeType(ENVELOPE_PATH_LINE_THINKESS));

            // --- 2. Draw the Interactive Points on Top of the Line ---
            for (size_t i = 0; i < m_mixTrack.envelopePoints.size(); ++i)
            {
                const auto &point = m_mixTrack.envelopePoints[i];
                const auto screenPos = envelopePointToScreenPosition(point);

                auto pointColor = juce::Colours::blue;
                float pointSize = ENVELOPE_POINT_STANDARD_RADIUS;

                if (m_selectedEnvelopePointIndex.has_value() && m_selectedEnvelopePointIndex.value() == i)
                {
                    pointColor = juce::Colours::red;
                    pointSize = ENVELOPE_POINT_ACTIVE_RADIUS;
                }
                else if (m_hoveredEnvelopePointIndex.has_value() && m_hoveredEnvelopePointIndex.value() == i)
                {
                    pointColor = juce::Colours::green;
                    pointSize = ENVELOPE_POINT_ACTIVE_RADIUS;
                }

                g.setColour(pointColor.withAlpha(1.0f));
                g.fillEllipse(screenPos.x - pointSize / 2.0f, screenPos.y - pointSize / 2.0f, pointSize, pointSize);
            }
        }

        void MixTrackComponent::drawAttachMarkers(juce::Graphics &g, juce::Rectangle<int> area)
        {
            // This lambda contains the drawing logic for a single marker.
            auto drawMarker = [&](MarkerType type, juce::Colour colour)
            {
                // Get the logical pixel position from our central helper function.
                const int x = getMarkerXPosition(type);
                // Check if this specific marker is being hovered over to apply a highlight.
                const bool isHovered = (m_hoveredMarker == type);

                // Only draw the marker if it falls within the component's visible bounds.
                if (x >= area.getX() && x <= area.getRight())
                {
                    // Draw the vertical line, brightening it on hover for visual feedback.
                    g.setColour(isHovered ? colour.brighter(0.5f) : colour);
                    g.drawVerticalLine(x, area.getY(), area.getBottom());

                    // Draw the circular handle at the top of the line.
                    const float handleSize = 8.0f;
                    juce::Rectangle<float> handle(x - handleSize / 2, area.getY() - handleSize / 2, handleSize, handleSize);
                    g.fillEllipse(handle);
                }
            };

            // Define the color for attach markers and call the drawing lambda for each one.
            const auto attachColor = juce::Colours::orange.withAlpha(0.5f);
            drawMarker(MarkerType::AttachFrom, attachColor);
            drawMarker(MarkerType::AttachTo, attachColor);
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
                spdlog::warn("getMarkerXPosition called with unsupported marker type: {}", static_cast<int>(marker));
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

        void MixTrackComponent::changeListenerCallback(juce::ChangeBroadcaster *source)
        {
            if (source == &m_thumbnail)
            {
                repaint();
            }
        }

        std::optional<size_t> MixTrackComponent::hitTestEnvelopePoint(juce::Point<int> mousePos) const
        {
            for (size_t i = 0; i < m_mixTrack.envelopePoints.size(); ++i)
            {
                // Get the corrected screen position for the envelope point.
                const auto pointScreenPos = envelopePointToScreenPosition(m_mixTrack.envelopePoints[i]).toInt();

                // Check the distance from the mouse to the point.
                if (mousePos.getDistanceFrom(pointScreenPos) <= ENVELOPE_POINT_ACTIVE_RADIUS)
                {
                    // We have a hit.
                    return i;
                }
            }
            // No points were hit.
            return std::nullopt;
        }

        juce::Point<float> MixTrackComponent::envelopePointToScreenPosition(const EnvelopePoint &point) const
        {
            auto area = getLocalBounds().removeFromBottom(waveformSectionHeight);

            const auto effectiveDuration = m_mixTrack.getEffectiveDuration(m_trackInfo.duration);
            if (effectiveDuration <= Duration_t{0})
            {
                return {(float)area.getX(), (float)area.getBottom()};
            }

            const auto timeRelativeToComponentStart = point.time - m_mixTrack.cueStart;

            const double proportion =
                std::chrono::duration<double>(timeRelativeToComponentStart).count() / std::chrono::duration<double>(effectiveDuration).count();

            const float x = (float)area.getX() + (float)proportion * (float)area.getWidth();
            const float y = (float)area.getBottom() - ((float)point.volume / (float)VOLUME_NORMALIZATION) * (float)area.getHeight();

            // Return a Point<float> as required by juce::Path. DO NOT round to int here.
            return {x, y};
        }

        EnvelopePoint MixTrackComponent::screenPositionToEnvelopePoint(juce::Point<int> screenPos) const
        {
            // Use the unified helper to get the time, clamped to the component's bounds.
            const auto newTime = xToTime(screenPos.x, true /* clampToComponentBounds */);

            // --- Volume Calculation (Y-axis) ---
            auto area = getLocalBounds().removeFromBottom(waveformSectionHeight);
            const float relativeY = (float)(area.getBottom() - screenPos.y) / (float)area.getHeight();
            const float volumePercent = juce::jlimit(0.0f, 1.0f, relativeY);

            database::EnvelopePoint result;
            result.time = newTime;
            result.volume = static_cast<Volume_t>(volumePercent * database::VOLUME_NORMALIZATION);
            return result;
        }

        MixTrackComponent::MarkerType MixTrackComponent::hitTestMarker(juce::Point<int> mousePos) const
        {
            auto bounds = getLocalBounds();
            auto waveformArea = bounds.removeFromBottom(waveformSectionHeight);
            
            // Only test within waveform area
            if (!waveformArea.contains(mousePos))
                return MarkerType::None;
                
            const int hitThreshold = ((int)ENVELOPE_POINT_ACTIVE_RADIUS); // pixels
            
            // Test each marker
            auto testMarker = [&](MarkerType type) -> bool
            {
                const int markerX = getMarkerXPosition(type);
                return std::abs(mousePos.x - markerX) <= hitThreshold;
            };
            
            if (testMarker(MarkerType::CueStart)) return MarkerType::CueStart;
            if (testMarker(MarkerType::CueEnd)) return MarkerType::CueEnd;
            if (testMarker(MarkerType::AttachFrom)) return MarkerType::AttachFrom;
            if (testMarker(MarkerType::AttachTo)) return MarkerType::AttachTo;
            
            return MarkerType::None;
        }
        
        
        Duration_t MixTrackComponent::xToTime(int x, bool clampToComponentBounds) const
        {
            const auto effectiveDuration = m_mixTrack.getEffectiveDuration(m_trackInfo.duration);
            if (getWidth() <= 0)
                return Duration_t{0};

            double proportion = (double)(x - getLocalBounds().getX()) / (double)getWidth();

            if (clampToComponentBounds)
            {
                proportion = juce::jlimit(0.0, 1.0, proportion);
            }

            const auto timeOffset = std::chrono::duration<double>(proportion * std::chrono::duration<double>(effectiveDuration).count());
            return m_mixTrack.cueStart + std::chrono::duration_cast<Duration_t>(timeOffset);
        }


        // Mouse Event Handlers --------------------------------------------------------
       
        void MixTrackComponent::mouseDown(const juce::MouseEvent &event)
        {
            if (event.mods.isLeftButtonDown())
            {
                // --- Priority 1: Check for an envelope point hit ---
                if (const auto hitPointIndex = hitTestEnvelopePoint(event.position.toInt()))
                {
                    m_selectedEnvelopePointIndex = hitPointIndex;
                    m_isDraggingEnvelopePoint = true;
                    m_envelopePointDragStart = event.position.toInt();
                    m_originalEnvelopePoint = m_mixTrack.envelopePoints[*hitPointIndex];
                }
                // --- Priority 2: Check for an attach marker hit ---
                else if (const auto markerHit = hitTestMarker(event.position.toInt()); markerHit != MarkerType::None)
                {
                    m_draggedMarker = markerHit;
                }
                if (auto *timeline = findParentComponentOfClass<TimelineComponent>())
                {
                    timeline->setSelectedTrack(this);
                }
                repaint();
            }
        }

        
        void MixTrackComponent::mouseDrag(const juce::MouseEvent &event)
        {
            // If an attach marker drag is in progress (m_draggedMarker is set),
            // this is where the logic to update its position would go.
            // Currently not implemented.
            if (m_draggedMarker != MarkerType::None)
            {
                // No-op for now.
            }
            // If an envelope point drag is in progress, update its position.
            else if (m_isDraggingEnvelopePoint && m_selectedEnvelopePointIndex.has_value())
            {
                // 1. Convert the current mouse position back to a logical EnvelopePoint.
                auto newPoint = screenPositionToEnvelopePoint(event.position.toInt());

                // 2. Apply constraints to the new point (e.g., can't move past neighbors).
                constrainEnvelopePoint(*m_selectedEnvelopePointIndex, newPoint);

                // 3. Update the data model with the new, constrained point position.
                //    Note: We must operate on a mutable copy of the MixTrack for this.
                //    This is a temporary solution; a better architecture would involve
                //    notifying a parent component to update the master data model.
                m_mixTrack.envelopePoints[*m_selectedEnvelopePointIndex] = newPoint;

                // 4. Trigger a repaint to show the point in its new position.
                repaint();
            }
        }

        
        void MixTrackComponent::constrainEnvelopePoint(size_t pointIndex, database::EnvelopePoint &point) const
        {
            if (pointIndex >= m_mixTrack.envelopePoints.size())
                return;

            // --- Rule 1: Maintain order between adjacent points ---
            // (This logic remains correct)
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

            // --- Rule 2: Clamp within the component's total effective duration ---
            // THIS IS THE FIX. We clamp to the real visual/logical boundaries.
            const auto trackDuration = m_trackInfo.duration;
            const auto effectiveStartTime = m_mixTrack.cueStart;
            const auto effectiveEndTime = m_mixTrack.getCueEndActual(trackDuration);

            point.time = std::max(point.time, effectiveStartTime);
            point.time = std::min(point.time, effectiveEndTime);

            // --- Volume constraint (remains correct) ---
            point.volume = juce::jlimit(Volume_t(0), database::VOLUME_NORMALIZATION, point.volume);
        }
        
        void MixTrackComponent::mouseUp(const juce::MouseEvent &event)
        {
            if (m_draggedMarker == MarkerType::CueEnd)
            {
                // 1. Calculate the new absolute time based on the mouse release position.
                const auto newAbsoluteTime = xToTime(event.position.x, false /* clampToComponentBounds */);

                // 2. Convert this absolute time to our storage format (offset from track end).
                MixTrack updatedTrack = m_mixTrack;
                updatedTrack.cueEnd = newAbsoluteTime - m_trackInfo.duration;

                // 3. Fire the callback to update the data model and trigger a layout refresh.
                if (onCueAttachChanged)
                {
                    onCueAttachChanged(m_trackInfo.trackId, updatedTrack);
                }

                // 4. Reset the drag state.
                m_draggedMarker = MarkerType::None;
                repaint(); // Repaint to show the new marker position after the layout has changed.
            }
            else if (m_draggedMarker != MarkerType::None)
            {
                // Notify of cue/attach change
                if (onCueAttachChanged)
                {
                    onCueAttachChanged(m_mixTrack.trackId, m_mixTrack);
                }

                m_draggedMarker = MarkerType::None;
                repaint();
            }
            else if (m_isDraggingEnvelopePoint)
            {
                // Notify of envelope change
                if (onEnvelopeChanged)
                {
                    onEnvelopeChanged(m_mixTrack.trackId, m_mixTrack.envelopePoints);
                }

                m_isDraggingEnvelopePoint = false;
                m_selectedEnvelopePointIndex = std::nullopt;
            }
        }

        void MixTrackComponent::mouseMove(const juce::MouseEvent &event)
        {
            // Check for marker hover - that is, over the attach lines, or the start/end of the track.
            const auto hoveredMarker = hitTestMarker(event.position.toInt());
            bool needsRepaint = false;

            if (hoveredMarker != m_hoveredMarker)
            {
                m_hoveredMarker = hoveredMarker;
                needsRepaint = true;
            }

            // Check for envelope point hover - both can be true at the same time
            const auto hoveredPoint = hitTestEnvelopePoint(event.position.toInt());
            if (hoveredPoint != m_hoveredEnvelopePointIndex)
            {
                m_hoveredEnvelopePointIndex = hoveredPoint;
                needsRepaint = true;
            }

            if (needsRepaint)
            {
                repaint();
            }

            // Update cursor if we're in j. edgar hoover land
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