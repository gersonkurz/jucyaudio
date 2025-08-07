#include <UI/TimelineComponent.h>
#include <spdlog/spdlog.h>
#include <toml++/toml.h> // Include the parser implementation here

namespace jucyaudio
{
    namespace ui
    {
        TimelineComponent::TimelineComponent(juce::AudioFormatManager &formatManager, juce::AudioThumbnailCache &thumbnailCache)
            : m_formatManager{formatManager},
              m_thumbnailCache{thumbnailCache},
              m_mixLoader{nullptr}
        {
            spdlog::info("[Timeline] Constructor called");
            setWantsKeyboardFocus(true);
            setInterceptsMouseClicks(true, true);  // Make sure we receive mouse clicks
            spdlog::info("[Timeline] Mouse interception enabled");
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

        bool TimelineComponent::deleteSelectedTrack()
        {
            // 1. Pre-condition checks
            if (!m_selectedTrack)
            {
                spdlog::warn("deleteSelectedTrack called but m_selectedTrack is null.");
                return false;
            }
            if (!m_mixLoader)
            {
                spdlog::error("deleteSelectedTrack called but m_mixLoader is null. Cannot proceed.");
                return false;
            }

            // 2. Get the ID of the track to delete
            const auto trackIdToRemove{m_selectedTrack->getTrackId()};
            if (trackIdToRemove == 0)
            {
                spdlog::error("Could not find TrackId for the selected component.");
                return false;
            }

            const auto currentMixId{m_mixLoader->getMixId()};
            spdlog::info("Attempting to delete Track ID: {} from Mix ID: {}", trackIdToRemove, currentMixId);

            // 3. Perform the database deletion
            if (!theTrackLibrary.getMixManager().removeTrackFromMix(currentMixId, trackIdToRemove))
            {
                spdlog::error("Failed to remove track from database.");
                return false;
            }

            spdlog::info("Successfully removed track from database.");

            // 4. CRITICAL: Refresh the in-memory loader from the database.
            // This synchronizes our data source with the change we just made.
            if (!m_mixLoader->reloadFromDatabase())
            {
                spdlog::error("Failed to reload MixProjectLoader from database after deletion!");
                return false;
            }

            spdlog::info("MixProjectLoader successfully reloaded from DB. It now has {} tracks.", m_mixLoader->getMixTracks().size());

            // 5. Repopulate the UI from the fresh, updated loader.
            // We are calling our own function with the loader we've just updated.
            return populateFrom();
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

            // Draw cue drag preview line (dashed orange line)
            if (m_cueDragPreviewTime.has_value())
            {
                const auto previewTimeSeconds = std::chrono::duration<double>(*m_cueDragPreviewTime).count();
                const float previewX = static_cast<float>(previewTimeSeconds * m_pixelsPerSecond);
                
                g.setColour(juce::Colours::orange.withAlpha(0.8f));
                
                // Draw a dashed line
                float dashLengths[] = { 4.0f, 4.0f };
                juce::Line<float> previewLine(previewX, 0.0f, previewX, static_cast<float>(getHeight()));
                g.drawDashedLine(previewLine, dashLengths, 2);
            }
        }

        void TimelineComponent::refreshLayout()
        {
            double maxTimeSecs = 0.0;
            for (const auto &view : m_trackViews)
            {
                // The start position is dynamic.
                const double startTime = std::chrono::duration<double>(view.componentStartTime).count();

                // The duration is dynamic.
                const double effectiveDuration = std::chrono::duration<double>(view.mixTrackData->getEffectiveDuration(view.trackInfoData->duration)).count();

                const double endTime = startTime + effectiveDuration;
                maxTimeSecs = std::max(maxTimeSecs, endTime);
            }

            m_calculatedWidth = static_cast<int>(maxTimeSecs * m_pixelsPerSecond) + 200;
            setSize(m_calculatedWidth, m_calculatedHeight);

            resized();
            repaint();
        }

        void TimelineComponent::repositionTrack(TrackId trackId)
        {
            // Find which track index this is
            int trackIndex = -1;
            for (size_t i = 0; i < m_trackViews.size(); ++i)
            {
                if (m_trackViews[i].mixTrackData && m_trackViews[i].mixTrackData->trackId == trackId)
                {
                    trackIndex = static_cast<int>(i);
                    break;
                }
            }
            
            if (trackIndex == -1)
                return;
            
            // Check if this is the first track or if attach points changed
            // For now, let's check if we need full recalculation
            bool needsFullRecalculation = (trackIndex == 0);
            
            // Also check if this track's attach points affect others
            // If attachTo changed, all subsequent tracks need updating
            // We can't easily detect what changed, so for safety, recalculate all if it's not the last track
            if (trackIndex < static_cast<int>(m_trackViews.size()) - 1)
            {
                needsFullRecalculation = true;
            }
            
            if (needsFullRecalculation)
            {
                // Recalculate all positions without recreating components
                recalculateTrackPositions();
            }
            else
            {
                // For the last track with only cueStart/cueEnd changes, just update that track
                auto& view = m_trackViews[trackIndex];
                view.componentStartTime = view.audioStartTime + view.mixTrackData->cueStart;
                
                // Trigger a layout update to reposition the component
                resized();
                repaint();
            }
        }

        void TimelineComponent::recalculateTrackPositions()
        {
            if (!m_mixLoader || m_trackViews.empty())
            {
                spdlog::warn("TimelineComponent::recalculateTrackPositions - No loader or tracks");
                return;
            }
            
            spdlog::info("TimelineComponent::recalculateTrackPositions - Processing {} tracks", m_trackViews.size());

            // Calculate the global offset from the first track's cueStart
            Duration_t globalOffset{0};
            if (m_trackViews[0].mixTrackData && m_trackViews[0].mixTrackData->cueStart < Duration_t{0})
            {
                globalOffset = -m_trackViews[0].mixTrackData->cueStart;
            }

            // Recalculate positions for all tracks
            Duration_t previousAudioStartTime{0};
            
            for (size_t i = 0; i < m_trackViews.size(); ++i)
            {
                auto& view = m_trackViews[i];
                if (!view.mixTrackData)
                    continue;

                // Calculate audio start time according to Mix Flow algorithm
                if (i == 0)
                {
                    view.audioStartTime = globalOffset;
                }
                else
                {
                    const auto& prevTrack = *m_trackViews[i - 1].mixTrackData;
                    view.audioStartTime = previousAudioStartTime + prevTrack.attachTo - view.mixTrackData->attachFrom;
                }

                // Update component start time
                view.componentStartTime = view.audioStartTime + view.mixTrackData->cueStart;
                
                previousAudioStartTime = view.audioStartTime;
            }

            // Refresh the layout with the new positions
            refreshLayout();
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
        }

        void TimelineComponent::playSelectedTrackFromPosition(double timePosition)
        {
        }

        void TimelineComponent::mouseDown(const juce::MouseEvent &event)
        {
            spdlog::info("[Timeline] mouseDown - position: ({}, {}), clicks: {}, leftButton: {}", 
                        event.position.x, event.position.y, event.getNumberOfClicks(), event.mods.isLeftButtonDown());
            
            // Always grab keyboard focus when the timeline is clicked.
            grabKeyboardFocus();

            if (event.mods.isLeftButtonDown())
            {
                // Convert the pixel x-coordinate to a time in seconds.
                double clickTime = event.position.x / m_pixelsPerSecond;
                spdlog::info("[Timeline] Click time: {} seconds (x={}, pixelsPerSecond={})", 
                            clickTime, event.position.x, m_pixelsPerSecond);

                // Update the visual playhead position.
                setCurrentTimePosition(clickTime);

                // Determine which track, if any, was under the cursor and select it.
                if (const auto clickedTrack = getTrackAtPosition(event.position.toInt()))
                {
                    spdlog::info("[Timeline] Track clicked at position");
                    setSelectedTrack(clickedTrack);
                }

                // --- Notify Parent of User Intent ---
                // This component does not handle playback directly. It only reports the
                // user's actions to the parent via callbacks.

                if (event.getNumberOfClicks() == 2 && onMixPlaybackAlwaysRequested)
                {
                    spdlog::info("[Timeline] Double-click detected, calling onMixPlaybackAlwaysRequested callback");
                    // A double-click is a request to start playback immediately.
                    onMixPlaybackAlwaysRequested(clickTime);
                }
                else if (event.getNumberOfClicks() == 1 && onSeekRequested)
                {
                    spdlog::info("[Timeline] Single-click detected, calling onSeekRequested callback");
                    // A single-click is a request to seek the transport.
                    onSeekRequested(clickTime);
                }
                else
                {
                    spdlog::info("[Timeline] Click detected but no callback set - clicks: {}, onMixPlaybackAlwaysRequested: {}, onSeekRequested: {}", 
                                event.getNumberOfClicks(), 
                                onMixPlaybackAlwaysRequested ? "set" : "null",
                                onSeekRequested ? "set" : "null");
                }

                repaint();
            }
        }

        MixTrackComponent *TimelineComponent::getTrackAtPosition(juce::Point<int> position) const
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
                refreshLayout();

                // Keep the time position under the mouse cursor stable
                maintainViewportPosition(timeAtMouse, mousePos.x);
            }
        }

        void TimelineComponent::viewportResized()
        {
            resized();
        }
        
        void TimelineComponent::resized()
        {
            auto visibleArea = getParentComponent()->getLocalBounds();
            const int rulerHeight = 30;
            const int trackHeight = MixTrackComponent::TOTAL_COMPONENT_HEIGHT;
            const int yGap = 5;
            
            // Always match the viewport height if we have one
            if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
            {
                const int viewportHeight = viewport->getHeight();
                
                // Always resize to match viewport height (don't check if already matching)
                if (viewportHeight != getHeight())
                {
                    setSize(getWidth(), viewportHeight);
                }
            }
            
            // Calculate available height for lanes using the actual component height
            const int availableHeightForLanes = getHeight() - rulerHeight;
            int numLanes = std::max(1, availableHeightForLanes / (trackHeight + yGap));
            int currentLane = 0;
            int laneDirection = +1;

            for (const auto &view : m_trackViews)
            {
                // The start position is now fully dynamic.
                const double startTime = std::chrono::duration<double>(view.componentStartTime).count();
                const int startX = static_cast<int>(startTime * m_pixelsPerSecond);

                // The width is now fully dynamic, based on the effective duration from the model.
                const double effectiveDuration = std::chrono::duration<double>(view.mixTrackData->getEffectiveDuration(view.trackInfoData->duration)).count();
                const int width = static_cast<int>(effectiveDuration * m_pixelsPerSecond);

                const int yPos = rulerHeight + (currentLane * (trackHeight + yGap));
                view.component->setBounds(startX, yPos, width, trackHeight);

                if ((currentLane + laneDirection) >= numLanes || (currentLane + laneDirection) < 0)
                    laneDirection *= -1;

                currentLane += laneDirection;
                if (numLanes == 1)
                    currentLane = 0;
            }
        }

        bool TimelineComponent::populateFrom(audio::MixProjectLoader *mixLoader)
        {
            if (!mixLoader)
            {
                if (!m_mixLoader)
                {
                    spdlog::error("TimelineComponent::populateFrom - mixLoader is null");
                    return false;
                }
                mixLoader = m_mixLoader;
            }
            else
            {
                m_mixLoader = mixLoader;
            }
            m_selectedTrack = nullptr;
            m_currentTimePosition = -1.0;
            m_trackViews.clear();
            removeAllChildren();
            
            spdlog::info("TimelineComponent::populateFrom - Starting with {} tracks", mixLoader->getMixTracks().size());

            // Create TrackView objects for each track
            for (size_t i = 0; i < mixLoader->getMixTracks().size(); ++i)
            {
                auto &mixTrack = mixLoader->getMixTracks()[i];
                if (const auto *trackInfo = mixLoader->getTrackInfoForId(mixTrack.trackId))
                {
                    TrackView view;
                    view.mixTrackData = &mixTrack;
                    view.trackInfoData = trackInfo;
                    // Initialize with default values - will be recalculated
                    view.audioStartTime = Duration_t{0};
                    view.componentStartTime = Duration_t{0};

                    view.component = std::make_unique<MixTrackComponent>(*view.mixTrackData, *view.trackInfoData, m_formatManager, m_thumbnailCache);

                    view.component->onCueAttachChanged = [this](TrackId id, const database::MixTrack &updatedTrack)
                    {
                        if (onCueAttachChanged)
                            onCueAttachChanged(id, updatedTrack);
                    };
                    view.component->onEnvelopeChanged = [this](TrackId id, const std::vector<database::EnvelopePoint> &points)
                    {
                        if (onEnvelopeChanged)
                            onEnvelopeChanged(id, points);
                    };
                    view.component->onCueDragInProgress = [this](TrackId trackId, bool isAttachPoint, std::optional<Duration_t> previewTime)
                    {
                        if (previewTime.has_value())
                        {
                            // Find the track view for this trackId
                            for (const auto& tv : m_trackViews)
                            {
                                if (tv.mixTrackData && tv.mixTrackData->trackId == trackId)
                                {
                                    // xToTime returns cueStart + offset_within_component
                                    // componentStartTime is where the component starts on the timeline
                                    // So absolute position = componentStartTime + offset_within_component
                                    m_cueDragPreviewTime = tv.componentStartTime + (*previewTime - tv.mixTrackData->cueStart);
                                    break;
                                }
                            }
                        }
                        else
                        {
                            m_cueDragPreviewTime = std::nullopt;
                        }
                        repaint();
                    };

                    addAndMakeVisible(*view.component);
                    m_trackViews.push_back(std::move(view));
                }
            }
            
            // --- THIS IS THE FIX ---
            // We must calculate a reasonable height for the timeline component itself
            // before we can calculate its width and trigger a layout refresh.
            const int trackHeight = MixTrackComponent::TOTAL_COMPONENT_HEIGHT;
            const int yGap = 5;
            const int rulerHeight = 30;
            const int numLanesForHeightCalc = 8; // A default number of lanes to ensure a reasonable minimum height.
            m_calculatedHeight = rulerHeight + (numLanesForHeightCalc * (trackHeight + yGap));
            
            // If we have a parent viewport, ensure we're at least as tall as its visible area
            if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
            {
                const int viewportHeight = viewport->getHeight();
                if (viewportHeight > m_calculatedHeight)
                {
                    m_calculatedHeight = viewportHeight;
                }
            }
            
            // Calculate positions for all tracks using the shared logic
            // This must be called AFTER setting m_calculatedHeight
            recalculateTrackPositions();
            
            return true;
        }

        void TimelineComponent::drawCrossfadeLines(juce::Graphics &g)
        {
            // For each consecutive pair of tracks, draw the attach/crossfade region
            for (size_t i = 0; i < m_trackViews.size(); ++i)
            {
                if (i + 1 >= m_trackViews.size())
                    break; // No next track to crossfade with

                const auto &currentView = m_trackViews[i];
                const auto &nextView = m_trackViews[i + 1];

                const auto &currentTrack = *currentView.mixTrackData;
                const auto &nextTrack = *nextView.mixTrackData;

                // The ATTACH point is calculated relative to the AUDIO start time, not the component start time.
                const double currentAudioStart = std::chrono::duration<double>(currentView.audioStartTime).count();
                const double attachToTime = currentAudioStart + std::chrono::duration<double>(currentTrack.attachTo).count();

                const float attachX = static_cast<float>(attachToTime * m_pixelsPerSecond);
                g.setColour(juce::Colours::orange.withAlpha(0.7f));
                g.drawVerticalLine(juce::roundToInt(attachX), 0.0f, static_cast<float>(getHeight()));

                g.setFont(10.0f);
                g.setColour(juce::Colours::orange);
                g.drawText("ATTACH", juce::roundToInt(attachX) - 20, 5, 40, 12, juce::Justification::centred);
            }
        }
    } // namespace ui
} // namespace jucyaudio