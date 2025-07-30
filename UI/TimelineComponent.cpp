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

        void TimelineComponent::playMixFromPosition(double timePosition)
        {
            spdlog::info("TimelineComponent::playMixFromPosition at time: {:.2f}", timePosition);
            
            if (onMixPlaybackRequested)
            {
                onMixPlaybackRequested(timePosition);
            }
            else
            {
                spdlog::warn("onMixPlaybackRequested callback is not set");
            }
        }

        bool TimelineComponent::keyPressed(const juce::KeyPress &key)
        {
            spdlog::info("TimelineComponent::keyPressed - key code: {}", key.getKeyCode());
            
            if (key == juce::KeyPress::spaceKey)
            {
                spdlog::info("Space key pressed - toggling playback");
                // Space: Play/stop entire mix from current position
                // If no position has been clicked, start from the beginning
                double playPosition = m_currentTimePosition >= 0.0 ? m_currentTimePosition : 0.0;
                playMixFromPosition(playPosition);
                return true;
            }
            else if (key == juce::KeyPress::escapeKey)
            {
                spdlog::info("Escape key pressed - stopping playback");
                // Escape: Stop playback
                if (onMixPlaybackRequested)
                {
                    onMixPlaybackRequested(-1.0); // Special value to indicate stop
                }
                return true;
            }
            else if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
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
            auto it = std::find_if(m_trackViews.begin(),
                m_trackViews.end(),
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
            // Draw crossfade lines that span across tracks
            drawCrossfadeLines(g);
            
            // Draw click position playhead (thin white line)
            if (m_currentTimePosition >= 0.0)
            {
                float playheadX = static_cast<float>(m_currentTimePosition * m_pixelsPerSecond);
                g.setColour(juce::Colours::white.withAlpha(0.5f));
                g.drawVerticalLine(juce::roundToInt(playheadX), 0.0f, static_cast<float>(getHeight()));
            }
            
            // Draw mix playback position (thick red line)
            if (m_mixPlaybackPosition >= 0.0)
            {
                float playheadX = static_cast<float>(m_mixPlaybackPosition * m_pixelsPerSecond);
                g.setColour(juce::Colours::red);
                g.fillRect(playheadX - 1.0f, 0.0f, 2.0f, static_cast<float>(getHeight()));

                // Draw a triangle at the top
                juce::Path playheadMarker;
                playheadMarker.addTriangle(playheadX - 6, 0, playheadX + 6, 0, playheadX, 12);
                g.fillPath(playheadMarker);
            }
        }

#if MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE
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

            m_calculatedWidth = static_cast<int>(maxTimeSecs * m_pixelsPerSecond) + 200;
            setSize(m_calculatedWidth, m_calculatedHeight);

            // This will trigger resized() which repositions all tracks
            resized();
            repaint();
        }
#endif

#if !MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE
        void TimelineComponent::recalculateLayout()
        {
            // Recalculate width based on new zoom and stored positions
            double maxTimeSecs = 0.0;
            for (const auto& view : m_trackViews)
            {
                const auto& trackInfo = *view.trackInfoData;
                const double startTime = std::chrono::duration<double>(view.calculatedStartTime).count();
                const double trackDuration = std::chrono::duration<double>(trackInfo.duration).count();
                const double endTime = startTime + trackDuration;
                
                if (endTime > maxTimeSecs)
                {
                    maxTimeSecs = endTime;
                }
            }

            m_calculatedWidth = static_cast<int>(maxTimeSecs * m_pixelsPerSecond) + 200;
            setSize(m_calculatedWidth, m_calculatedHeight);

            // This will trigger resized() which repositions all tracks
            resized();
            repaint();
        }
#endif

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
#if MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE
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
#endif
        }

        void TimelineComponent::playSelectedTrackFromPosition(double timePosition)
        {
#if MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE
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
#endif
        }

        void TimelineComponent::mouseDown(const juce::MouseEvent &event)
        {
            // Grab keyboard focus when clicked
            grabKeyboardFocus();
            
            if (event.mods.isLeftButtonDown())
            {
                double clickTime = event.position.x / m_pixelsPerSecond;
                spdlog::info("Timeline clicked at time: {:.2f}s", clickTime);

                setCurrentTimePosition(clickTime);

                MixTrackComponent *clickedTrack = getTrackAtPosition(event.position.toInt());
                setSelectedTrack(clickedTrack);

                if (event.getNumberOfClicks() == 2)
                {
                    spdlog::info("Double-click detected - playing mix from position");
                    // Double-click = set position + always play mix (don't toggle)
                    if (onMixPlaybackAlwaysRequested)
                    {
                        onMixPlaybackAlwaysRequested(clickTime);
                    }
                    else
                    {
                        // Fallback to regular playback if the new callback isn't set
                        playMixFromPosition(clickTime);
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

        void TimelineComponent::refreshLayout()
        {
            // Recalculate timeline width based on effective durations
            double maxTimeSecs = 0.0;
            for (const auto& view : m_trackViews)
            {
                const double startTime = std::chrono::duration<double>(view.calculatedStartTime).count();
                const double effectiveDuration = std::chrono::duration<double>(
                    view.mixTrackData->getEffectiveDuration(view.trackInfoData->duration)).count();
                const double endTime = startTime + effectiveDuration;
                maxTimeSecs = std::max(maxTimeSecs, endTime);
            }
            
            m_calculatedWidth = static_cast<int>(maxTimeSecs * m_pixelsPerSecond) + 200;
            setSize(m_calculatedWidth, m_calculatedHeight);
            
            // Trigger a layout recalculation
            resized();
            repaint();
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
#if MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE
        void TimelineComponent::resized()
        {
            //spdlog::info("LAYOUT_START: TimelineComponent::resized() -----------------------");

            auto visibleArea = getParentComponent()->getLocalBounds();
            const int rulerHeight = 30;
            const int trackHeight = MixTrackComponent::totalHeight;
            const int yGap = 5;
            const int availableHeightForLanes = visibleArea.getHeight() - rulerHeight;

            //spdlog::info("LAYOUT_INFO: visibleArea={}x{}, availableHeight={}", visibleArea.getWidth(), visibleArea.getHeight(), availableHeightForLanes);

            int numLanes = availableHeightForLanes / (trackHeight + yGap);
            if (numLanes < 1)
                numLanes = 1;

            int currentLane = 0;
            int laneDirection = +1;

            for (const auto &view : m_trackViews)
            {
                const auto startTime = std::chrono::duration<double>(view.mixTrackData->mixStartTime).count();

                // With envelope system, tracks play their full duration
                const auto trackDuration = std::chrono::duration<double>(view.trackInfoData->duration).count();

                const int startX = static_cast<int>(startTime * m_pixelsPerSecond);
                const int width = static_cast<int>(trackDuration * m_pixelsPerSecond);
                const int yPos = rulerHeight + (currentLane * (trackHeight + yGap));

                // Log BEFORE setting bounds
                /* spdlog::info("LAYOUT_TRACK: Track {}, startTime={:.3f}s, duration={:.3f}s, startX={}, width={}, yPos={}, currentBounds=({},{},{}x{})",
                    view.mixTrackData->trackId,
                    startTime,
                    trackDuration,
                    startX,
                    width,
                    yPos,
                    view.component->getX(),
                    view.component->getY(),
                    view.component->getWidth(),
                    view.component->getHeight());
                    */

                view.component->setBounds(startX, yPos, width, trackHeight);

                // Log AFTER setting bounds
                /* spdlog::info("LAYOUT_SET: Track {}, newBounds=({},{},{}x{})",
                    view.mixTrackData->trackId,
                    view.component->getX(),
                    view.component->getY(),
                    view.component->getWidth(),
                    view.component->getHeight());
                    */
                // Update lane logic
                if ((currentLane + laneDirection) >= numLanes || (currentLane + laneDirection) < 0)
                {
                    laneDirection *= -1;
                }
                currentLane += laneDirection;
                if (numLanes == 1)
                    currentLane = 0;
            }

            //spdlog::info("LAYOUT_END: TimelineComponent::resized() finished -----------------------");
        }
#endif

#if !MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE
        void TimelineComponent::resized()
        {
            auto visibleArea = getParentComponent()->getLocalBounds();
            const int rulerHeight = 30;
            const int trackHeight = MixTrackComponent::totalHeight;
            const int yGap = 5;
            const int availableHeightForLanes = visibleArea.getHeight() - rulerHeight;

            int numLanes = std::max(1, availableHeightForLanes / (trackHeight + yGap));
            int currentLane = 0;
            int laneDirection = +1;

            // Define our hardcoded test value
            const double silenceExtensionSeconds = 15.0;

            for (const auto &view : m_trackViews)
            {
                const auto &trackInfo = *view.trackInfoData;
                const auto &mixTrack = *view.mixTrackData;

                // The component's start position does not change.
                const double startTime = std::chrono::duration<double>(view.calculatedStartTime).count();
                const int startX = static_cast<int>(startTime * m_pixelsPerSecond);

                // Calculate the width based on the REAL effective duration...
                const double effectiveDuration = std::chrono::duration<double>(mixTrack.getEffectiveDuration(trackInfo.duration)).count();
                const int baseWidth = static_cast<int>(effectiveDuration * m_pixelsPerSecond);

                // ...and then ADD our hardcoded silence extension.
                const int extensionWidth = static_cast<int>(silenceExtensionSeconds * m_pixelsPerSecond);
                const int totalWidth = baseWidth + extensionWidth;

                const int yPos = rulerHeight + (currentLane * (trackHeight + yGap));

                view.component->setBounds(startX, yPos, totalWidth, trackHeight);

                // Update lane logic
                if ((currentLane + laneDirection) >= numLanes || (currentLane + laneDirection) < 0)
                {
                    laneDirection *= -1;
                }
                currentLane += laneDirection;
                if (numLanes == 1)
                    currentLane = 0;
            }
        }
#endif

#if MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE
        void TimelineComponent::populateFrom(audio::MixProjectLoader &mixLoader)
        {
            m_selectedTrack = nullptr;
            m_currentTimePosition = -1.0;
            // Clear all existing components
            m_trackViews.clear();
            removeAllChildren();

            for (const auto &mixTrack : mixLoader.getMixTracks())
            {
                if (const auto *trackInfo = mixLoader.getTrackInfoForId(mixTrack.trackId))
                {
                    //spdlog::info("Adding track ID: {} to timeline: {}-{}", mixTrack.trackId,
                    //    trackInfo->artist_name, trackInfo->title);
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
#endif

#if !MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE
        void TimelineComponent::populateFrom(audio::MixProjectLoader &mixLoader)
        {
            m_selectedTrack = nullptr;
            m_currentTimePosition = -1.0;
            // Clear all existing components
            m_trackViews.clear();
            removeAllChildren();

            // Calculate timeline positions using ATTACH model
            Duration_t previousTrackStart{0};
            
            for (size_t i = 0; i < mixLoader.getMixTracks().size(); ++i)
            {
                const auto &mixTrack = mixLoader.getMixTracks()[i];
                if (const auto *trackInfo = mixLoader.getTrackInfoForId(mixTrack.trackId))
                {
                    Duration_t trackStart{0};
                    
                    if (i == 0)
                    {
                        // First track starts at position 0
                        trackStart = Duration_t{0};
                    }
                    else
                    {
                        // ATTACH formula: Next track start = Previous track start + Previous track's attachTo - Current track's attachFrom
                        const auto& prevTrack = mixLoader.getMixTracks()[i-1];
                        trackStart = previousTrackStart + prevTrack.attachTo - mixTrack.attachFrom;
                    }
                    
                    TrackView view;
                    view.mixTrackData = &mixLoader.getMixTracks()[i]; // Point to the actual data
                    view.trackInfoData = trackInfo;
                    view.component = std::make_unique<MixTrackComponent>(*view.mixTrackData, *view.trackInfoData, m_formatManager, m_thumbnailCache);
                    view.calculatedStartTime = trackStart; // Store the calculated start time
                    
                    // Set up callbacks
                    view.component->onCueAttachChanged = [this](TrackId id, const database::MixTrack& updatedTrack)
                    {
                        if (onCueAttachChanged)
                            onCueAttachChanged(id, updatedTrack);
                    };
                    
                    view.component->onEnvelopeChanged = [this](TrackId id, const std::vector<database::EnvelopePoint>& points)
                    {
                        if (onEnvelopeChanged)
                            onEnvelopeChanged(id, points);
                    };
                    
                    addAndMakeVisible(*view.component, m_trackViews.size());
                    m_trackViews.push_back(std::move(view));
                    
                    // Remember this track's start for the next iteration
                    previousTrackStart = trackStart;
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

            // Calculate width based on mix duration
            // Need to account for extended tracks beyond their original duration
            double maxTimeSecs = 0.0;
            for (const auto& view : m_trackViews)
            {
                const double startTime = std::chrono::duration<double>(view.calculatedStartTime).count();
                const double effectiveDuration = std::chrono::duration<double>(
                    view.mixTrackData->getEffectiveDuration(view.trackInfoData->duration)).count();
                const double endTime = startTime + effectiveDuration;
                maxTimeSecs = std::max(maxTimeSecs, endTime);
            }
            m_calculatedWidth = static_cast<int>(maxTimeSecs * m_pixelsPerSecond) + 200;

            // Set the component's size to its calculated ideal size
            setSize(m_calculatedWidth, m_calculatedHeight);

            // Force the layout calculation
            spdlog::info("Forcing resized() call after populateFrom (ATTACH-based)");
            resized();
            repaint();
        }
#endif

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
                //spdlog::info("TIMELINE_DRAG: Track at time {:.3f}s, draggingTrack={}", newTime, m_draggingTrack != nullptr);

                static double lastUpdateTime = -1.0;
                if (std::abs(newTime - lastUpdateTime) > 0.1)
                {
                    lastUpdateTime = newTime;
                    //spdlog::info("TIMELINE_UPDATE: Significant time change to {:.3f}s", newTime);
                }
            }
        }

        void TimelineComponent::finishTrackDrag(MixTrackComponent *track, double finalTime)
        {
            if (m_draggingTrack == track)
            {
                //spdlog::info("Timeline: Finished drag at time: {:.2f}s", finalTime);

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
#if MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE
            // Sort tracks by their mixStartTime and update orderInMix
            std::vector<TrackView *> sortedViews;
            for (auto &view : m_trackViews)
            {
                sortedViews.push_back(&view);
            }

            std::sort(sortedViews.begin(),
                sortedViews.end(),
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
#endif
        }
        
        void TimelineComponent::drawCrossfadeLines(juce::Graphics &g)
        {
            // For each consecutive pair of tracks, draw the attach/crossfade region
            for (size_t i = 0; i < m_trackViews.size(); ++i)
            {
                if (i + 1 >= m_trackViews.size())
                    break; // No next track to crossfade with
                    
                const auto& currentView = m_trackViews[i];
                const auto& nextView = m_trackViews[i + 1];
                
                const auto& currentTrack = *currentView.mixTrackData;
                const auto& nextTrack = *nextView.mixTrackData;
                
                // Calculate where current track's attachTo point is on the timeline
                const double currentTrackStart = std::chrono::duration<double>(currentView.calculatedStartTime).count();
                const double attachToTime = currentTrackStart + std::chrono::duration<double>(currentTrack.attachTo).count();
                
                // Calculate where next track's attachFrom point is on the timeline
                const double nextTrackStart = std::chrono::duration<double>(nextView.calculatedStartTime).count();
                const double attachFromTime = nextTrackStart + std::chrono::duration<double>(nextTrack.attachFrom).count();
                
                // The attach point is where these two tracks connect
                // According to ATTACH formula: next track starts at (prev start + prev attachTo - next attachFrom)
                // So the connection point on the timeline is at attachToTime
                
                // Draw vertical line at the attach point
                const float attachX = static_cast<float>(attachToTime * m_pixelsPerSecond);
                g.setColour(juce::Colours::orange.withAlpha(0.7f));
                g.drawVerticalLine(juce::roundToInt(attachX), 0.0f, static_cast<float>(getHeight()));
                
                // Draw label at top
                g.setFont(10.0f);
                g.setColour(juce::Colours::orange);
                g.drawText("ATTACH", juce::roundToInt(attachX) - 20, 5, 40, 12, juce::Justification::centred);
            }
        }
    } // namespace ui
} // namespace jucyaudio