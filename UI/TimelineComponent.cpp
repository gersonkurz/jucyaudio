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
                    refreshLayout();

                    // Keep the time position under the mouse cursor stable
                    maintainViewportPosition(timeAtMouse, mousePos.x);
                }
            }
        }

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
        
        void TimelineComponent::populateFrom(audio::MixProjectLoader &mixLoader)
        {
            m_selectedTrack = nullptr;
            m_currentTimePosition = -1.0;
            m_trackViews.clear();
            removeAllChildren();

            Duration_t previousAudioStartTime{0};

            // --- FINAL GENERALIZED FIX ---
            // The global offset is of type Duration_t (milliseconds) to maintain precision.
            Duration_t globalOffset{0};

            // 1. Determine the global offset from the actual cueStart of the first track.
            if (!mixLoader.getMixTracks().empty())
            {
                auto &firstTrack = mixLoader.getMixTracks().front();
                if (firstTrack.cueStart < Duration_t{0})
                {
                    // The offset is the absolute value of the silence needed.
                    // No duration_cast is needed as both types are milliseconds.
                    globalOffset = -firstTrack.cueStart;
                }
            }

            for (size_t i = 0; i < mixLoader.getMixTracks().size(); ++i)
            {
                auto &mixTrack = mixLoader.getMixTracks()[i];
                if (const auto *trackInfo = mixLoader.getTrackInfoForId(mixTrack.trackId))
                {
                    Duration_t currentAudioStartTime{0};
                    if (i == 0)
                    {
                        // 2. The first track's audio starts at our dynamic global offset.
                        currentAudioStartTime = globalOffset;
                    }
                    else
                    {
                        const auto &prevTrack = mixLoader.getMixTracks()[i - 1];
                        currentAudioStartTime = previousAudioStartTime + prevTrack.attachTo - prevTrack.attachFrom;
                    }

                    TrackView view;
                    view.mixTrackData = &mixTrack;
                    view.trackInfoData = trackInfo;

                    view.audioStartTime = currentAudioStartTime;
                    view.componentStartTime = currentAudioStartTime + mixTrack.cueStart;

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

                    addAndMakeVisible(*view.component);
                    m_trackViews.push_back(std::move(view));

                    previousAudioStartTime = currentAudioStartTime;
                }
            }
            // --- THIS IS THE FIX ---
            // We must calculate a reasonable height for the timeline component itself
            // before we can calculate its width and trigger a layout refresh.
            const int trackHeight = MixTrackComponent::totalHeight;
            const int yGap = 5;
            const int rulerHeight = 30;
            const int numLanesForHeightCalc = 8; // A default number of lanes to ensure a reasonable minimum height.
            m_calculatedHeight = rulerHeight + (numLanesForHeightCalc * (trackHeight + yGap));
            refreshLayout();
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