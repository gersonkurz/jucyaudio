#include <UI/TimelineComponent.h>
#include <spdlog/spdlog.h>
#include <toml++/toml.h> // Include the parser implementation here

namespace jucyaudio
{
    namespace ui
    {
        TimelineComponent::TimelineComponent(juce::AudioFormatManager &formatManager, juce::AudioThumbnailCache &thumbnailCache)
            : m_formatManager{formatManager},
              m_thumbnailCache{thumbnailCache}
        {
            setWantsKeyboardFocus(true);  
        }

        bool TimelineComponent::keyPressed(const juce::KeyPress &key)
        {
            if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
            {
                if (m_selectedTrack)
                {
                    spdlog::info("Delete key pressed - removing selected track");
                    deleteSelectedTrack();
                    return true; // Consumed the key event
                }
            }

            return false; // Let parent handle other keys
        }

        void TimelineComponent::deleteSelectedTrack()
        {
            if (!m_selectedTrack)
                return;

            spdlog::info("Deleting selected track from timeline");

            // Find which track view corresponds to the selected component
            auto it = std::find_if(m_trackViews.begin(), m_trackViews.end(),
                                   [this](const TrackView &view)
                                   {
                                       return view.component.get() == m_selectedTrack;
                                   });

            if (it != m_trackViews.end())
            {
                // Get the track ID before we remove it (for data model update)
                TrackId trackIdToRemove = it->mixTrackData->trackId;

                // Remove from UI
                removeChildComponent(it->component.get());

                // Remove from our track views
                m_trackViews.erase(it);

                // Clear selection since we just deleted the selected track
                m_selectedTrack = nullptr;

                // Notify the parent that we need to update the data model
                if (onTrackDeleted)
                {
                    onTrackDeleted(trackIdToRemove);
                }

                // Recalculate layout and repaint
                resized();
                repaint();

                spdlog::info("Track {} removed from timeline", trackIdToRemove);
            }
        }

        void TimelineComponent::paint(juce::Graphics &g)
        {
            // Draw the main background
            g.fillAll(getLookAndFeel().findColour(juce::TreeView::backgroundColourId));

            // Draw the time grid
            g.setColour(getLookAndFeel().findColour(juce::TextEditor::outlineColourId));
            const int numMarkers = static_cast<int>(getWidth() / (30 * m_pixelsPerSecond));
            for (int i = 0; i <= numMarkers; ++i)
            {
                const float x = static_cast<float>(i * 30 * m_pixelsPerSecond);
                g.drawVerticalLine(juce::roundToInt(x), 0.0f, static_cast<float>(getHeight()));

                int minutes = (i * 30) / 60;
                int seconds = (i * 30) % 60;
                juce::String time = juce::String::formatted("%d:%02d", minutes, seconds);
                g.drawText(time, juce::roundToInt(x) + 4, 4, 100, 20, juce::Justification::topLeft);
            }
        }

        void TimelineComponent::paintOverChildren(juce::Graphics &g)
        {
            // Draw playhead OVER all the tracks
            if (m_currentTimePosition >= 0.0)
            {
                float playheadX = static_cast<float>(m_currentTimePosition * m_pixelsPerSecond);
                g.setColour(juce::Colours::red);
                g.drawVerticalLine(juce::roundToInt(playheadX), 0.0f, static_cast<float>(getHeight()));

                // Optional: draw a small triangle at the top
                juce::Path playheadMarker;
                playheadMarker.addTriangle(playheadX - 5, 0, playheadX + 5, 0, playheadX, 10);
                g.fillPath(playheadMarker);
            }
        }

        void TimelineComponent::recalculateLayout()
        {
            // Recalculate width based on new zoom
            double maxTimeSecs = 0.0;
            if (!m_trackViews.empty())
            {
                const auto &lastView = m_trackViews.back();
                const auto startTime = std::chrono::duration<double>(lastView.mixTrackData->mixStartTime).count();
                const auto duration = std::chrono::duration<double>(lastView.trackInfoData->duration).count();
                maxTimeSecs = startTime + duration;
            }

            m_calculatedWidth = static_cast<int>(maxTimeSecs * m_pixelsPerSecond)  + 200;
            setSize(m_calculatedWidth, m_calculatedHeight);

            // This will trigger resized() which repositions all tracks
            resized();
            repaint();
        }

        void TimelineComponent::maintainViewportPosition(double timeAtMouse, int mouseX)
        {
            if (auto *viewport = findParentComponentOfClass<juce::Viewport>())
            {
                // Calculate where that time position should be after zoom
                int newMouseX = static_cast<int>(timeAtMouse * m_pixelsPerSecond);

                // Adjust viewport to keep the time position under the cursor
                auto currentViewPos = viewport->getViewPosition();
                int deltaX = newMouseX - mouseX;
                viewport->setViewPosition(currentViewPos.x + deltaX, currentViewPos.y);
            }
        }

        void TimelineComponent::playFromPosition(double timePosition)
        {
            // For now, play the first track that covers this time position
            // Later this could play the entire mix from this position
            for (const auto &view : m_trackViews)
            {
                const auto startTime = std::chrono::duration<double>(view.mixTrackData->mixStartTime).count();
                const auto endTime = startTime + std::chrono::duration<double>(view.trackInfoData->duration).count();

                if (timePosition >= startTime && timePosition <= endTime)
                {
                    // Calculate offset within the track
                    double trackOffset = timePosition - startTime;

                    juce::File audioFile(view.trackInfoData->filepath.string());
                    if (onPlaybackRequested)
                    {
                        onPlaybackRequested(audioFile, trackOffset);
                    }
                    break;
                }
            }
        }

        void TimelineComponent::playSelectedTrackFromPosition(double timePosition)
        {
            if (!m_selectedTrack)
                return;

            // Find the corresponding track data
            for (const auto &view : m_trackViews)
            {
                if (view.component.get() == m_selectedTrack)
                {
                    const auto startTime = std::chrono::duration<double>(view.mixTrackData->mixStartTime).count();
                    double trackOffset = timePosition - startTime;

                    // Clamp to valid range
                    trackOffset = juce::jlimit(0.0, std::chrono::duration<double>(view.trackInfoData->duration).count(), trackOffset);

                    juce::File audioFile(view.trackInfoData->filepath.string());
                    if (onPlaybackRequested)
                    {
                        onPlaybackRequested(audioFile, trackOffset);
                    }
                    break;
                }
            }
        }

        void TimelineComponent::mouseDown(const juce::MouseEvent &event)
        {
            if (event.mods.isLeftButtonDown())
            {
                double clickTime = event.position.x / m_pixelsPerSecond;
                spdlog::info("Timeline clicked at time: {:.2f}s", clickTime);

                setCurrentTimePosition(clickTime);

                MixTrackComponent *clickedTrack = getTrackAtPosition(event.position.toInt());
                setSelectedTrack(clickedTrack);

                if (event.getNumberOfClicks() == 2)
                {
                    spdlog::info("Double-click detected - requesting playback");
                    if (clickedTrack)
                    {
                        playSelectedTrackFromPosition(clickTime);
                    }
                    else
                    {
                        playFromPosition(clickTime);
                    }
                }
                else if (event.getNumberOfClicks() == 1)
                {
                    spdlog::info("Single-click detected - checking seek callback");
                    if (onSeekRequested)
                    {
                        spdlog::info("Calling seek callback with time: {:.2f}", clickTime);
                        onSeekRequested(clickTime);
                    }
                    else
                    {
                        spdlog::warn("onSeekRequested callback is null!");
                    }
                }

                repaint();
            }
        }

        MixTrackComponent *TimelineComponent::getTrackAtPosition(juce::Point<int> position)
        {
            for (auto &view : m_trackViews)
            {
                if (view.component->getBounds().contains(position))
                {
                    return view.component.get();
                }
            }
            return nullptr;
        }

        void TimelineComponent::setSelectedTrack(MixTrackComponent *track)
        {
            if (m_selectedTrack != track)
            {
                // Repaint old selection
                if (m_selectedTrack)
                    m_selectedTrack->repaint();

                m_selectedTrack = track;

                // Repaint new selection
                if (m_selectedTrack)
                    m_selectedTrack->repaint();
            }
        }

        void TimelineComponent::setCurrentTimePosition(double timeInSeconds)
        {
            if (m_currentTimePosition != timeInSeconds)
            {
                m_currentTimePosition = timeInSeconds;
                repaint(); // Redraw playhead
            }
        }

        void TimelineComponent::mouseWheelMove(const juce::MouseEvent &event, const juce::MouseWheelDetails &wheel)
        {
            if (event.mods.isCtrlDown())
            {
                // Get mouse position relative to timeline
                auto mousePos = event.getPosition();

                // Calculate time position at mouse cursor
                double timeAtMouse = mousePos.x / m_pixelsPerSecond;

                // Calculate new zoom level
                double zoomDelta = wheel.deltaY > 0 ? ZOOM_FACTOR : (1.0 / ZOOM_FACTOR);
                double newZoom = juce::jlimit(MIN_ZOOM, MAX_ZOOM, m_pixelsPerSecond * zoomDelta);

                if (newZoom != m_pixelsPerSecond)
                {
                    m_pixelsPerSecond = newZoom;
                    recalculateLayout();

                    // Keep the time position under the mouse cursor stable
                    maintainViewportPosition(timeAtMouse, mousePos.x);
                }
            }
        }
        void TimelineComponent::resized()
        {
            spdlog::info("TimelineComponent::resized() called -----------------------");
            // --- THIS IS THE CRITICAL CHANGE ---
            // We do NOT use getHeight() here. getHeight() returns the *current* size of the component,
            // which might be the large size we just set.
            // We need the size of the VISIBLE AREA, which is the Viewport's size.
            // We can get this from our parent component, which is the Viewport's own content area.

            // This is the width and height of the visible "window" onto our timeline.
            auto visibleArea = getParentComponent()->getLocalBounds();

            const int rulerHeight = 30;
            const int trackHeight = MixTrackComponent::totalHeight;
            const int yGap = 5;

            const int availableHeightForLanes = visibleArea.getHeight() - rulerHeight;
            spdlog::info("Available height for tracks: {}", availableHeightForLanes);
            int numLanes = availableHeightForLanes / (trackHeight + yGap);
            if (numLanes < 1)
                numLanes = 1;

            spdlog::info("Number of lanes available: {}", numLanes);

            int currentLane = 0;
            int laneDirection = +1; // +1 for downhill, -1 for uphill

            for (const auto &view : m_trackViews)
            {
                const auto startTime = std::chrono::duration<double>(view.mixTrackData->mixStartTime).count();
                auto trackDuration = view.mixTrackData->cutoffTime > std::chrono::milliseconds(0)
                                         ? std::chrono::duration<double>(view.mixTrackData->cutoffTime).count()
                                         : std::chrono::duration<double>(view.trackInfoData->duration).count();

                const int startX = static_cast<int>(startTime * m_pixelsPerSecond);
                const int width = static_cast<int>(trackDuration * m_pixelsPerSecond);

                // Calculate the Y position based on the current lane.
                const int yPos = rulerHeight + (currentLane * (trackHeight + yGap));
                view.component->setBounds(startX, yPos, width, trackHeight);

                spdlog::info("Setting bounds for track ID {}: x={}, y={}, width={}, height={}", view.mixTrackData->trackId, startX, yPos, width, trackHeight);
                // --- Update the lane for the next track ---
                // If we are about to go out of bounds...
                if ((currentLane + laneDirection) >= numLanes || (currentLane + laneDirection) < 0)
                {
                    spdlog::info("Changing lane direction at lane {} (numLanes: {})", currentLane, numLanes);
                    // Reverse direction.
                    laneDirection *= -1;
                    spdlog::info("New lane direction: {}", laneDirection);
                }

                // Move to the next lane.
                currentLane += laneDirection;
                spdlog::info("Moving to lane: {}", currentLane);

                // A special case for when numLanes is 1, to prevent getting stuck.
                if (numLanes == 1)
                    currentLane = 0;
            }
            spdlog::info("TimelineComponent::resized() finished -----------------------");
        }

        void TimelineComponent::populateFrom(const audio::MixProjectLoader &mixLoader)
        {
            m_selectedTrack = nullptr;
            m_currentTimePosition = 0.0;
            // Clear all existing components
            m_trackViews.clear();
            removeAllChildren();

            for (const auto &mixTrack : mixLoader.getMixTracks())
            {
                if (const auto *trackInfo = mixLoader.getTrackInfoForId(mixTrack.trackId))
                {
                    spdlog::info("Adding track ID: {} to timeline", mixTrack.trackId);
                    TrackView view;
                    view.mixTrackData = &mixTrack;
                    view.trackInfoData = trackInfo;
                    view.component = std::make_unique<MixTrackComponent>(*view.mixTrackData, *view.trackInfoData, m_formatManager, m_thumbnailCache);
                    addAndMakeVisible(*view.component, m_trackViews.size());
                    m_trackViews.push_back(std::move(view));
                }
                else
                {
                    spdlog::warn("Track info not found for track ID: {}", mixTrack.trackId);
                }
            }

            // Calculate size
            const int trackHeight = MixTrackComponent::totalHeight;
            const int yGap = 5;
            const int rulerHeight = 30;
            const int numLanesForHeightCalc = 8;
            m_calculatedHeight = rulerHeight + (numLanesForHeightCalc * (trackHeight + yGap));

            double maxTimeSecs = 0.0;
            if (!m_trackViews.empty())
            {
                const auto &lastView = m_trackViews.back();
                const auto startTime = std::chrono::duration<double>(lastView.mixTrackData->mixStartTime).count();
                const auto duration = std::chrono::duration<double>(lastView.trackInfoData->duration).count();
                maxTimeSecs = startTime + duration;
            }
            m_calculatedWidth = static_cast<int>(maxTimeSecs * m_pixelsPerSecond) + 200;

            // Set the component's size to its calculated ideal size
            setSize(m_calculatedWidth, m_calculatedHeight);

            // FORCE the layout calculation - this is the missing piece!
            spdlog::info("Forcing resized() call after populateFrom");
            resized();
            repaint();
        }
    } // namespace ui
} // namespace jucyaudio