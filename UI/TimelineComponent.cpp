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
            spdlog::info("LAYOUT_START: TimelineComponent::resized() -----------------------");

            auto visibleArea = getParentComponent()->getLocalBounds();
            const int rulerHeight = 30;
            const int trackHeight = MixTrackComponent::totalHeight;
            const int yGap = 5;
            const int availableHeightForLanes = visibleArea.getHeight() - rulerHeight;

            spdlog::info("LAYOUT_INFO: visibleArea={}x{}, availableHeight={}", visibleArea.getWidth(), visibleArea.getHeight(), availableHeightForLanes);

            int numLanes = availableHeightForLanes / (trackHeight + yGap);
            if (numLanes < 1)
                numLanes = 1;

            int currentLane = 0;
            int laneDirection = +1;

            for (const auto &view : m_trackViews)
            {
                const auto startTime = std::chrono::duration<double>(view.mixTrackData->mixStartTime).count();
                auto trackDuration = view.mixTrackData->cutoffTime > std::chrono::milliseconds(0)
                                         ? std::chrono::duration<double>(view.mixTrackData->cutoffTime).count()
                                         : std::chrono::duration<double>(view.trackInfoData->duration).count();

                const int startX = static_cast<int>(startTime * m_pixelsPerSecond);
                const int width = static_cast<int>(trackDuration * m_pixelsPerSecond);
                const int yPos = rulerHeight + (currentLane * (trackHeight + yGap));

                // Log BEFORE setting bounds
                spdlog::info("LAYOUT_TRACK: Track {}, startTime={:.3f}s, startX={}, width={}, yPos={}, currentBounds=({},{},{}x{})", view.mixTrackData->trackId,
                             startTime, startX, width, yPos, view.component->getX(), view.component->getY(), view.component->getWidth(),
                             view.component->getHeight());

                view.component->setBounds(startX, yPos, width, trackHeight);

                // Log AFTER setting bounds
                spdlog::info("LAYOUT_SET: Track {}, newBounds=({},{},{}x{})", view.mixTrackData->trackId, view.component->getX(), view.component->getY(),
                             view.component->getWidth(), view.component->getHeight());

                // Update lane logic
                if ((currentLane + laneDirection) >= numLanes || (currentLane + laneDirection) < 0)
                {
                    laneDirection *= -1;
                }
                currentLane += laneDirection;
                if (numLanes == 1)
                    currentLane = 0;
            }

            spdlog::info("LAYOUT_END: TimelineComponent::resized() finished -----------------------");

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

        void TimelineComponent::startTrackDrag(MixTrackComponent *track)
        {
            m_draggingTrack = track;
            setSelectedTrack(track);

            spdlog::info("Timeline: Started drag for track at time position: {:.2f}s", track->getX() / m_pixelsPerSecond);
        }

        void TimelineComponent::updateTrackDrag(MixTrackComponent *track, double newTime)
        {
            if (m_draggingTrack == track)
            {
                spdlog::info("TIMELINE_DRAG: Track at time {:.3f}s, draggingTrack={}", newTime, m_draggingTrack != nullptr);

                static double lastUpdateTime = -1.0;
                if (std::abs(newTime - lastUpdateTime) > 0.1)
                {
                    lastUpdateTime = newTime;
                    spdlog::info("TIMELINE_UPDATE: Significant time change to {:.3f}s", newTime);
                }
            }
        }

        void TimelineComponent::finishTrackDrag(MixTrackComponent *track, double finalTime)
        {
            if (m_draggingTrack == track)
            {
                spdlog::info("Timeline: Finished drag at time: {:.2f}s", finalTime);

                // Find the track data and update it
                auto trackId = getTrackIdForComponent(track);
                if (trackId != 0)
                {
                    updateTrackPosition(trackId, finalTime);
                }

                m_draggingTrack = nullptr;

                // Recalculate layout after position change
                recalculateTrackOrder();
                resized();
                repaint();

                // Notify that mix data has changed
                if (onMixChanged)
                {
                    onMixChanged();
                }
            }
        }

        // Helper method to get track ID from component
        TrackId TimelineComponent::getTrackIdForComponent(MixTrackComponent *component)
        {
            for (const auto &view : m_trackViews)
            {
                if (view.component.get() == component)
                {
                    return view.mixTrackData->trackId;
                }
            }
            return 0; // Not found
        }

        // Update track position in the data model
        void TimelineComponent::updateTrackPosition(TrackId trackId, double newTimeInSeconds)
        {
            // We need access to the MixProjectLoader to modify the data
            // This will require getting a reference to it from MixEditorComponent

            // For now, let's assume we have a callback to get the mix data
            if (onTrackPositionChanged)
            {
                auto newStartTime = std::chrono::milliseconds(static_cast<int64_t>(newTimeInSeconds * 1000));
                onTrackPositionChanged(trackId, newStartTime);
            }
        }

        // Method to recalculate track order based on new positions
        void TimelineComponent::recalculateTrackOrder()
        {
            // Sort tracks by their mixStartTime and update orderInMix
            std::vector<TrackView *> sortedViews;
            for (auto &view : m_trackViews)
            {
                sortedViews.push_back(&view);
            }

            std::sort(sortedViews.begin(), sortedViews.end(),
                      [](const TrackView *a, const TrackView *b)
                      {
                          return a->mixTrackData->mixStartTime < b->mixTrackData->mixStartTime;
                      });

            // Update orderInMix values
            for (size_t i = 0; i < sortedViews.size(); ++i)
            {
                // This is a const_cast because we need to modify the data
                // We'll need to get non-const access to the mix data
                const_cast<database::MixTrack *>(sortedViews[i]->mixTrackData)->orderInMix = static_cast<int>(i);
            }

            spdlog::info("Recalculated track order for {} tracks", sortedViews.size());
        }
    } // namespace ui
} // namespace jucyaudio