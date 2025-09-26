#include "BinaryData.h"
#include <Database/Includes/Constants.h>
#include <UI/MixTrackComponent.h>
#include <UI/CustomColourIds.h>
#include <UI/TimelineComponent.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{

    namespace ui
    {
        using namespace database;

        MixTrackComponent::MixTrackComponent(
            MixTrack &mixTrack, const TrackInfo &trackInfo, juce::AudioFormatManager &formatManager, juce::AudioThumbnailCache &thumbnailCache)
            : m_mixTrack{mixTrack},
              m_trackInfo{trackInfo},
              m_thumbnail{2048, formatManager, thumbnailCache}
        {
            m_thumbnail.addChangeListener(this);

            // --- Waveform Caching Logic ---
            std::vector<unsigned char> cachedWaveformVec;
            auto &db = theTrackLibrary;
            if (db.loadWaveform(m_trackInfo.trackId, cachedWaveformVec).isOk() && !cachedWaveformVec.empty())
            {
                // Cache hit: Load from blob
                juce::MemoryBlock mb{cachedWaveformVec.data(), cachedWaveformVec.size()};
                juce::MemoryInputStream stream{mb, false};
                if (m_thumbnail.loadFrom(stream))
                {
                    spdlog::info("Loaded waveform for track {} from cache.", m_trackInfo.trackId);
                    m_isLoaded = true;
                }
                else
                {
                    spdlog::error("Failed to load waveform from cached blob for track {}. Regenerating.", m_trackInfo.trackId);
                    generateThumbnailFromFile();
                }
            }
            else
            {
                // Cache miss: Generate from file and save to cache later
                generateThumbnailFromFile();
            }

            const auto infoText{
                std::format("{} - {} - {} ({})", m_trackInfo.artist_name, m_trackInfo.album_title, m_trackInfo.title, durationToString(m_trackInfo.duration))};
            m_infoLabel.setText(infoText, juce::dontSendNotification);
            m_infoLabel.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(m_infoLabel);

            // Initialize m_gainSlider
            m_gainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
            m_gainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
            m_gainSlider.setRange(0.0, 4.0, 0.01); // Range from 0.0 to 2.0, with 0.01 step
            m_gainSlider.setDoubleClickReturnValue(true, 1.0); // Allow double-click to reset to 1.0
            
            // Temporarily remove listener to prevent callback during initialization
            m_gainSlider.removeListener(this); 
            m_gainSlider.setValue(m_mixTrack.gainAdjustment, juce::dontSendNotification); // Set initial value without sending notification
            m_gainSlider.addListener(this); // Re-add listener

            addAndMakeVisible(m_gainSlider);
        }

        MixTrackComponent::~MixTrackComponent()
        {
            m_thumbnail.removeChangeListener(this);
        }

        void MixTrackComponent::paint(juce::Graphics &g)
        {
            // Track how many paints are happening and measure individual paint time
            static int paintCallCount = 0;
            static int visiblePaintCount = 0;
            static int culledPaintCount = 0;
            static auto lastReportTime = std::chrono::high_resolution_clock::now();
            static long totalPaintMicros = 0;
            
            paintCallCount++;
            
            auto now = std::chrono::high_resolution_clock::now();
            auto timeSinceReport = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReportTime);
            if (timeSinceReport.count() >= 1000) // Report every second
            {
                spdlog::info("MixTrackComponent paint stats:");
                spdlog::info("  Total paint calls: {} (visible: {}, culled: {})", 
                            paintCallCount, visiblePaintCount, culledPaintCount);
                if (visiblePaintCount > 0)
                {
                    spdlog::info("  Avg paint time: {} µs/paint", totalPaintMicros / visiblePaintCount);
                }
                paintCallCount = 0;
                visiblePaintCount = 0;
                culledPaintCount = 0;
                totalPaintMicros = 0;
                lastReportTime = now;
            }
            
            // Viewport culling optimization: Check if this component is visible horizontally
            // Get the viewport bounds in our parent's coordinate space
            if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
            {
                auto viewArea = viewport->getViewArea();
                auto ourBoundsInParent = getBoundsInParent();
                
                // Check if we're visible in the viewport horizontally (with small margin)
                const int margin = 100; // Small margin to ensure smooth scrolling
                if (ourBoundsInParent.getRight() < (viewArea.getX() - margin) ||
                    ourBoundsInParent.getX() > (viewArea.getRight() + margin))
                {
                    // We're completely outside the viewport horizontally, skip painting
                    culledPaintCount++;
                    return;
                }
            }
            
            visiblePaintCount++;
            
            // --- 1. Basic Setup & Background ---
            auto &lf = getLookAndFeel();
            auto bounds = getLocalBounds();

            g.setColour(
                isSelected() ? lf.findColour(juce::TextEditor::backgroundColourId).brighter(0.2f) : lf.findColour(juce::TextEditor::backgroundColourId));
            g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

            auto waveformArea = bounds.removeFromBottom(WAVEFORM_SECTION_HEIGHT);

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
                auto waveformColour = isSelected()
                                          ? lf.findColour(juce::TreeView::selectedItemBackgroundColourId).withAlpha(0.5f)
                                          : lf.findColour(jucyaudio::ui::waveformColourId).withAlpha(0.7f);
                g.setColour(waveformColour);

                // Log zoom-related info for debugging
                spdlog::info("MixTrackComponent paint - track {}: component width={}, waveformDrawRect width={}, pixelsPerSecond={}, bounds=({},{},{},{})",
                             m_mixTrack.orderInMix, getWidth(), waveformDrawRect.getWidth(), m_pixelsPerSecond,
                             getBounds().getX(), getBounds().getY(), getBounds().getWidth(), getBounds().getHeight());

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
            g.strokePath(volumePath, juce::PathStrokeType(ENVELOPE_PATH_LINE_THICKESS));

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
                // AttachFrom is relative to the START of the source audio, not an absolute position
                // We need to convert it to the same coordinate system as cueStart/cueEnd
                markerTime = m_mixTrack.attachFrom;
                break;
            case MarkerType::AttachTo:
                // AttachTo is relative to the START of the source audio, not an absolute position
                // We need to convert it to the same coordinate system as cueStart/cueEnd
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
            // Track resized calls
            static int resizedCallCount = 0;
            static auto lastReportTime = std::chrono::high_resolution_clock::now();
            resizedCallCount++;
            
            auto now = std::chrono::high_resolution_clock::now();
            auto timeSinceReport = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReportTime);
            if (timeSinceReport.count() >= 1000)
            {
                spdlog::info("MixTrackComponent::resized called {} times in last second", resizedCallCount);
                resizedCallCount = 0;
                lastReportTime = now;
            }
            
            spdlog::debug("Track ID: {}, Component Width: {}", m_mixTrack.trackId, getWidth());

            auto bounds = getLocalBounds();
            auto topSectionBounds = bounds.removeFromTop(TEXT_SECTION_HEIGHT);
            spdlog::debug("topSectionBounds initial width: {}", topSectionBounds.getWidth());

            // Define desired slider width (including padding)
            const int desiredSliderWidth = 150 + 8; // 150px for slider + 4px padding on each side

            // Calculate available width for the slider
            int availableWidthForSlider = topSectionBounds.getWidth();
            int actualSliderWidth = std::min(desiredSliderWidth, availableWidthForSlider);

            // Position the slider on the right
            m_gainSlider.setBounds(topSectionBounds.removeFromRight(actualSliderWidth).reduced(4, 0));
            spdlog::debug("m_gainSlider width: {}", m_gainSlider.getWidth());

            // The remaining topSectionBounds is for the label
            m_infoLabel.setBounds(topSectionBounds.reduced(4, 0));
            spdlog::debug("m_infoLabel width: {}", m_infoLabel.getWidth());
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
                if (m_thumbnail.isFullyLoaded() && !m_isLoaded)
                {
                    m_isLoaded = true;
                    spdlog::info("Thumbnail fully generated for track {}, saving to cache.", m_trackInfo.trackId);

                    juce::MemoryOutputStream stream;
                    m_thumbnail.saveTo(stream);
                    const auto &block = stream.getMemoryBlock();
                    std::vector<unsigned char> blobData;
                    blobData.resize(block.getSize());
                    memcpy(blobData.data(), block.getData(), block.getSize());
                    theTrackLibrary.saveWaveform(m_trackInfo.trackId, blobData);
                }
                repaint();
            }
        }

        void MixTrackComponent::generateThumbnailFromFile()
        {
            const auto trackPath = m_trackInfo.reconstructFullPath();
            if (trackPath.empty())
            {
                spdlog::error("Could not reconstruct path for track {}. Cannot generate thumbnail.", m_trackInfo.trackId);
                return;
            }

            spdlog::info("Generating waveform for track {} from file: {}", m_trackInfo.trackId, pathToString(trackPath));
            m_thumbnail.setSource(new juce::FileInputSource{juce::File(ui::jucePathFromFs(trackPath))});
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
            auto area = getLocalBounds().removeFromBottom(WAVEFORM_SECTION_HEIGHT);

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
            auto area = getLocalBounds().removeFromBottom(WAVEFORM_SECTION_HEIGHT);
            const float relativeY = (float)(area.getBottom() - screenPos.y) / (float)area.getHeight();
            const float volumePercent = juce::jlimit(0.0f, 1.0f, relativeY);

            EnvelopePoint result;
            result.time = newTime;
            result.volume = static_cast<Volume_t>(volumePercent * VOLUME_NORMALIZATION);
            return result;
        }

        MixTrackComponent::MarkerType MixTrackComponent::hitTestMarker(juce::Point<int> mousePos) const
        {
            auto bounds = getLocalBounds();
            auto waveformArea = bounds.removeFromBottom(WAVEFORM_SECTION_HEIGHT);

            // Only test within waveform area
            if (!waveformArea.contains(mousePos))
                return MarkerType::None;

            const int hitThreshold = 5; // pixels

            // Test each marker
            auto testMarker = [&](MarkerType type) -> bool
            {
                const int markerX = getMarkerXPosition(type);
                return std::abs(mousePos.x - markerX) <= hitThreshold;
            };

            if (testMarker(MarkerType::CueStart))
                return MarkerType::CueStart;
            if (testMarker(MarkerType::CueEnd))
                return MarkerType::CueEnd;
            if (testMarker(MarkerType::AttachFrom))
                return MarkerType::AttachFrom;
            if (testMarker(MarkerType::AttachTo))
                return MarkerType::AttachTo;

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
            spdlog::info("[MixTrackComponent] mouseDown - clicks: {}, position: ({}, {})", event.getNumberOfClicks(), event.position.x, event.position.y);

            // Check for right-click (context menu)
            if (event.mods.isPopupMenu())
            {
                // First select this track
                if (auto *timeline = findParentComponentOfClass<TimelineComponent>())
                {
                    timeline->setSelectedTrack(this);
                }

                // Show context menu
                showContextMenu(event);
                return;
            }

            if (event.mods.isLeftButtonDown())
            {
                // Check for double-click first - pass it to the timeline for playback
                if (event.getNumberOfClicks() == 2)
                {
                    spdlog::info("[MixTrackComponent] Double-click detected, forwarding to timeline");
                    if (auto *timeline = findParentComponentOfClass<TimelineComponent>())
                    {
                        // Convert local coordinates to timeline coordinates
                        auto timelinePos = timeline->getLocalPoint(this, event.position);
                        spdlog::info("[MixTrackComponent] Local pos: ({}, {}), Timeline pos: ({}, {})",
                            event.position.x,
                            event.position.y,
                            timelinePos.x,
                            timelinePos.y);

                        // Create a new mouse event in timeline's coordinate space
                        juce::MouseEvent timelineEvent(event.source,
                            timelinePos,
                            event.mods,
                            event.pressure,
                            event.orientation,
                            event.rotation,
                            event.tiltX,
                            event.tiltY,
                            event.eventComponent,
                            event.originalComponent,
                            event.eventTime,
                            event.mouseDownPosition,
                            event.mouseDownTime,
                            event.getNumberOfClicks(),
                            event.mouseWasDraggedSinceMouseDown());

                        // Forward the event to timeline
                        timeline->mouseDown(timelineEvent);
                        return; // Don't process further
                    }
                }

                // --- Priority 1: Check for an envelope point hit ---
                if (const auto hitPointIndex = hitTestEnvelopePoint(event.position.toInt()))
                {
                    m_selectedEnvelopePointIndex = hitPointIndex;
                    m_isDraggingEnvelopePoint = true;
                    m_envelopePointDragStart = event.position.toInt();
                    m_originalEnvelopePoint = m_mixTrack.envelopePoints[*hitPointIndex];
                    grabKeyboardFocus(); // Ensure we can receive ESC key
                }
                // --- Priority 2: Check for an attach marker hit ---
                else if (const auto markerHit = hitTestMarker(event.position.toInt()); markerHit != MarkerType::None)
                {
                    m_draggedMarker = markerHit;
                    grabKeyboardFocus(); // Ensure we can receive ESC key
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
            // If a cue marker drag is in progress, calculate preview position
            if (m_draggedMarker == MarkerType::CueStart || m_draggedMarker == MarkerType::CueEnd)
            {
                // Calculate the new absolute time based on current mouse position
                const auto previewTime = xToTime(event.position.x, false /* clampToComponentBounds */);

                // Fire the callback to show preview line
                if (onCueDragInProgress)
                {
                    onCueDragInProgress(m_mixTrack.orderInMix, false /* not attach point */, previewTime);
                }
            }
            else if (m_draggedMarker == MarkerType::AttachFrom || m_draggedMarker == MarkerType::AttachTo)
            {
                // Calculate the new time for the attach point
                auto previewTime = xToTime(event.position.x, false /* clampToComponentBounds */);
                
                // Constrain attach points to valid range
                const auto effectiveStart = m_mixTrack.cueStart;
                const auto effectiveEnd = m_mixTrack.getCueEndActual(m_trackInfo.duration);
                previewTime = std::max(previewTime, effectiveStart);
                previewTime = std::min(previewTime, effectiveEnd);

                // Show preview line for attach points too
                if (onCueDragInProgress)
                {
                    onCueDragInProgress(m_mixTrack.orderInMix, true /* is attach point */, previewTime);
                }
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

        void MixTrackComponent::constrainEnvelopePoint(size_t pointIndex, EnvelopePoint &point) const
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
            point.volume = juce::jlimit(Volume_t(0), VOLUME_NORMALIZATION, point.volume);
        }

        void MixTrackComponent::mouseUp(const juce::MouseEvent &event)
        {
            if (m_draggedMarker == MarkerType::CueStart)
            {
                // 1. Calculate the new absolute time based on the mouse release position.
                const auto newAbsoluteTime = xToTime(event.position.x, false /* clampToComponentBounds */);

                // 2. Update cueStart directly with the new absolute time.
                MixTrack updatedTrack = m_mixTrack;
                updatedTrack.cueStart = newAbsoluteTime;

                // 3. Fire the callback to update the data model and trigger a layout refresh.
                if (onCueAttachChanged)
                {
                    onCueAttachChanged(m_mixTrack.orderInMix, updatedTrack);
                }

                // 4. Clear the preview line
                if (onCueDragInProgress)
                {
                    onCueDragInProgress(m_mixTrack.orderInMix, false, std::nullopt);
                }

                // 5. Reset the drag state.
                m_draggedMarker = MarkerType::None;
                repaint();
            }
            else if (m_draggedMarker == MarkerType::CueEnd)
            {
                // 1. Calculate the new absolute time based on the mouse release position.
                const auto newAbsoluteTime = xToTime(event.position.x, false /* clampToComponentBounds */);

                // 2. Convert this absolute time to our storage format (offset from track end).
                // cueEnd is the offset from the END of the track
                // newAbsoluteTime is where we want the cue-end marker to be
                // The natural end of the track (without cueEnd) would be at: cueStart + trackDuration
                // So: cueEnd = newAbsoluteTime - (cueStart + trackDuration)
                MixTrack updatedTrack = m_mixTrack;
                updatedTrack.cueEnd = newAbsoluteTime - (m_mixTrack.cueStart + m_trackInfo.duration);
                
                // 3. Fire the callback to update the data model and trigger a layout refresh.
                if (onCueAttachChanged)
                {
                    onCueAttachChanged(m_mixTrack.orderInMix, updatedTrack);
                }

                // 4. Clear the preview line
                if (onCueDragInProgress)
                {
                    onCueDragInProgress(m_mixTrack.orderInMix, false, std::nullopt);
                }

                // 5. Reset the drag state.
                m_draggedMarker = MarkerType::None;
                repaint(); // Repaint to show the new marker position after the layout has changed.
            }
            else if (m_draggedMarker == MarkerType::AttachFrom || m_draggedMarker == MarkerType::AttachTo)
            {
                // Calculate the new time for the attach point
                auto newTime = xToTime(event.position.x, false /* clampToComponentBounds */);

                // Constrain attach points to valid range
                const auto effectiveStart = m_mixTrack.cueStart;
                const auto effectiveEnd = m_mixTrack.getCueEndActual(m_trackInfo.duration);
                newTime = std::max(newTime, effectiveStart);
                newTime = std::min(newTime, effectiveEnd);

                // Update the appropriate attach point
                MixTrack updatedTrack = m_mixTrack;
                if (m_draggedMarker == MarkerType::AttachFrom)
                {
                    updatedTrack.attachFrom = newTime;
                }
                else // AttachTo
                {
                    updatedTrack.attachTo = newTime;
                }

                // Fire the callback
                if (onCueAttachChanged)
                {
                    onCueAttachChanged(m_mixTrack.orderInMix, updatedTrack);
                }

                // Clear preview line
                if (onCueDragInProgress)
                {
                    onCueDragInProgress(m_mixTrack.orderInMix, true, std::nullopt);
                }

                m_draggedMarker = MarkerType::None;
                repaint();
            }
            else if (m_draggedMarker != MarkerType::None)
            {
                // This shouldn't happen, but just in case
                m_draggedMarker = MarkerType::None;
                repaint();
            }
            else if (m_isDraggingEnvelopePoint)
            {
                // Notify of envelope change
                if (onEnvelopeChanged)
                {
                    onEnvelopeChanged(m_mixTrack.orderInMix, m_mixTrack.envelopePoints);
                }

                m_isDraggingEnvelopePoint = false;
                m_selectedEnvelopePointIndex = std::nullopt;
            }
        }

        void MixTrackComponent::mouseMove(const juce::MouseEvent &event)
        {
            bool needsRepaint = false;

            // Check for envelope point hover first (higher priority)
            const auto hoveredPoint = hitTestEnvelopePoint(event.position.toInt());
            if (hoveredPoint != m_hoveredEnvelopePointIndex)
            {
                m_hoveredEnvelopePointIndex = hoveredPoint;
                needsRepaint = true;
            }

            // Check for marker hover only if no envelope point is hovered
            const auto hoveredMarker = hoveredPoint.has_value() ? MarkerType::None : hitTestMarker(event.position.toInt());
            if (hoveredMarker != m_hoveredMarker)
            {
                m_hoveredMarker = hoveredMarker;
                needsRepaint = true;
            }

            if (needsRepaint)
            {
                repaint();
            }

            // Update cursor based on what we're hovering over (envelope points have priority)
            if (hoveredPoint.has_value())
            {
                // Envelope points always get pointing hand cursor
                setMouseCursor(juce::MouseCursor::PointingHandCursor);
            }
            else if (hoveredMarker == MarkerType::CueStart || hoveredMarker == MarkerType::CueEnd)
            {
                // Use resize cursor for edge dragging
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            }
            else if (hoveredMarker == MarkerType::AttachFrom || hoveredMarker == MarkerType::AttachTo)
            {
                // Use pointing hand for attach markers
                setMouseCursor(juce::MouseCursor::PointingHandCursor);
            }
            else
            {
                setMouseCursor(juce::MouseCursor::NormalCursor);
            }
        }

        bool MixTrackComponent::keyPressed(const juce::KeyPress &key)
        {
            if (key == juce::KeyPress::escapeKey)
            {
                // Cancel any ongoing drag operation
                if (m_draggedMarker != MarkerType::None || m_isDraggingEnvelopePoint)
                {
                    // Clear the preview line
                    if (m_draggedMarker != MarkerType::None && onCueDragInProgress)
                    {
                        bool isAttach = (m_draggedMarker == MarkerType::AttachFrom || m_draggedMarker == MarkerType::AttachTo);
                        onCueDragInProgress(m_mixTrack.orderInMix, isAttach, std::nullopt);
                    }

                    // Reset drag state
                    m_draggedMarker = MarkerType::None;
                    m_isDraggingEnvelopePoint = false;
                    m_selectedEnvelopePointIndex = std::nullopt;

                    // Restore original envelope point if we were dragging one
                    if (m_selectedEnvelopePointIndex.has_value() && m_selectedEnvelopePointIndex.value() < m_mixTrack.envelopePoints.size())
                    {
                        m_mixTrack.envelopePoints[*m_selectedEnvelopePointIndex] = m_originalEnvelopePoint;
                    }

                    repaint();
                    return true; // Key was handled
                }
            }

            return false; // Key not handled
        }

        void MixTrackComponent::showContextMenu(const juce::MouseEvent &event)
        {
            juce::PopupMenu menu;

            // Get timeline to check clipboard state
            auto *timeline = findParentComponentOfClass<TimelineComponent>();
            const bool hasClipboard = timeline ? timeline->hasClipboardData() : false;

            // Add menu items with IDs - enable/disable based on state
            menu.addItem(1, "Cut", true);  // Always enabled when track is selected
            menu.addItem(2, "Copy", true); // Always enabled when track is selected
            menu.addSeparator();
            menu.addItem(3, "Paste Before", hasClipboard);
            menu.addItem(4, "Paste After", hasClipboard);
            menu.addSeparator();
            menu.addItem(5, "Delete", true); // Always enabled when track is selected
            menu.addSeparator();
            menu.addItem(6, "Remove All Following Tracks", true);

            // Show the menu and handle the result
            menu.showMenuAsync(juce::PopupMenu::Options(),
                [this](int result)
                {
                    if (result != 0)
                    {
                        handleContextMenuResult(result);
                    }
                });
        }

        void MixTrackComponent::handleContextMenuResult(int menuItemID)
        {
            auto *timeline = findParentComponentOfClass<TimelineComponent>();
            if (!timeline)
            {
                spdlog::error("[MixTrackComponent] No parent timeline found");
                return;
            }

            switch (menuItemID)
            {
            case 1: // Cut
                spdlog::info("[MixTrackComponent] Context menu: Cut selected");
                timeline->cutSelectedTrackToClipboard();
                break;

            case 2: // Copy
                spdlog::info("[MixTrackComponent] Context menu: Copy selected");
                timeline->copySelectedTrackToClipboard();
                break;

            case 3: // Paste Before
                spdlog::info("[MixTrackComponent] Context menu: Paste Before selected");
                timeline->pasteFromClipboard(true);
                break;

            case 4: // Paste After
                spdlog::info("[MixTrackComponent] Context menu: Paste After selected");
                timeline->pasteFromClipboard(false);
                break;

            case 5: // Delete
                spdlog::info("[MixTrackComponent] Context menu: Delete selected");
                // Use existing deleteSelectedTrack method
                timeline->deleteSelectedTrack();
                break;

            case 6: // Remove All Following Tracks
                spdlog::info("[MixTrackComponent] Context menu: Remove All Following Tracks selected");
                timeline->removeAllTracksAfterSelected();
                break;

            default:
                break;
            }
        }

        void MixTrackComponent::sliderValueChanged(juce::Slider* slider)
        {
            if (slider == &m_gainSlider)
            {
                m_mixTrack.gainAdjustment = (float)m_gainSlider.getValue();
                if (onGainAdjustmentChanged)
                {
                    onGainAdjustmentChanged(m_mixTrack.orderInMix, m_mixTrack.gainAdjustment);
                }
            }
        }

    } // namespace ui
} // namespace jucyaudio