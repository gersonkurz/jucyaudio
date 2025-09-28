#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/IMixMarkerManager.h>
#include <Database/BackgroundTasks/WaveformLoadingTask.h>
#include <UI/MainComponent.h>
#include <UI/MixEditorComponent.h>
#include <UI/PlaybackController.h>
#include <UI/Settings.h>
#include <UI/TaskDialog.h>
#include <UI/VirtualTimelineComponent.h>
#include <Utils/StringWriter.h>
#include <Utils/UiUtils.h>
#include <Database/TrackLibrary.h>

namespace jucyaudio
{
    namespace ui
    {

        MixEditorComponent::MixEditorComponent()
            : m_timeline{m_formatManager, m_thumbnailCache}
        {
            m_formatManager.registerBasicFormats();
            setWantsKeyboardFocus(true);
            
            // Check config for virtual timeline preference
            m_useVirtualTimeline = config::theSettings.mixEditingSettings.useVirtualTimeline;
            
            // Setup virtual timeline if enabled
            if (m_useVirtualTimeline)
            {
                setupVirtualTimeline();
            }

            // Add the marker ruler at the top
            addAndMakeVisible(m_markerRuler);
            
            // Set up marker callbacks
            m_markerRuler.onMarkerAdded = [this](std::chrono::milliseconds position)
            {
                handleMarkerAdd(position);
            };
            
            m_markerRuler.onMarkerClicked = [this](MarkerId markerId)
            {
                handleMarkerClick(markerId);
            };

            // Set the timeline as the component to be viewed by the viewport.
            if (m_useVirtualTimeline && m_virtualTimeline)
            {
                m_viewport.setViewedComponent(m_virtualTimeline.get(), false);
            }
            else
            {
                m_viewport.setViewedComponent(&m_timeline, false); // false = don't delete when replaced
            }
            m_viewport.setScrollBarsShown(true, true);
            
            // Listen for scroll events
            m_viewport.getHorizontalScrollBar().addListener(this);
            m_viewport.getVerticalScrollBar().addListener(this);
            
            addAndMakeVisible(m_viewport);
            
            // Add the playhead overlay on top of the viewport
            m_viewport.addAndMakeVisible(m_playheadOverlay);
                        
            m_timeline.onCueAttachChanged = [this](int orderInMix, const database::MixTrack& updatedTrack)
            {
                updateCueAttachInData(orderInMix, updatedTrack);
            };
            
            m_timeline.onEnvelopeChanged = [this](int orderInMix, const std::vector<database::EnvelopePoint>& points)
            {
                updateEnvelopeInData(orderInMix, points);
            };

            m_timeline.onMixChanged = [this]()
            {
                saveMixChanges();
            };
            
            // Set up mix playback callbacks
            m_timeline.onMixPlaybackRequested = [this](double startTime)
            {
                handleMixPlayback(startTime, false);
            };
            
            // Set up double-click callback (always play)
            m_timeline.onMixPlaybackAlwaysRequested = [this](double startTime)
            {
                handleMixPlayback(startTime, true);
            };
            
            // Set up zoom change callback
            m_timeline.onZoomChanged = [this]()
            {
                // Update overlay when zoom changes to keep playhead in sync
                updatePlayheadOverlayPosition();
                
                if (m_useVirtualTimeline && m_virtualTimeline)
                {
                    const double pixelsPerSecond = m_timeline.getPixelsPerSecond();
                    const double secondsPerPixel = 1.0 / pixelsPerSecond;
                    m_virtualTimeline->setZoomLevel(secondsPerPixel);
                }
            };
            
            // Set up seek callback for clicking on timeline
            m_timeline.onSeekRequested = [this](double timePosition)
            {
                if (m_playbackController && m_playbackController->isMixMode())
                {
                    m_playbackController->seek(timePosition);
                    m_timeline.setMixPlaybackPosition(timePosition);
                    
                    if (m_useVirtualTimeline && m_virtualTimeline)
                    {
                        m_virtualTimeline->setPlayheadPosition(timePosition);
                    }
                }
            };
        }
        
        MixEditorComponent::~MixEditorComponent()
        {
            // Timer removed - now handled by TimerMultiplexer
            m_viewport.getHorizontalScrollBar().removeListener(this);
            m_viewport.getVerticalScrollBar().removeListener(this);
            // Unload mix before cleanup
            unloadMix();
        }

        void MixEditorComponent::forceRefresh()
        {
            m_timeline.repaint();
            m_viewport.repaint();
            resized(); // Recalculate viewport content
        }

        void MixEditorComponent::setPlaybackController(PlaybackController* controller)
        {
            m_playbackController = controller;
        }

        void MixEditorComponent::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ListBox::backgroundColourId).darker());
            
            // Draw read-only indicator if mix is locked/exported
            if (m_isReadOnly)
            {
                g.setColour(juce::Colours::orange.withAlpha(0.8f));
                g.setFont(14.0f);
                const auto text = "🔒 Mix is Read-Only (Exported) - Right-click in tree to unlock";
                const auto textWidth = g.getCurrentFont().getStringWidth(text);
                const auto x = getWidth() - textWidth - 10;
                const auto y = 5;
                
                // Draw background for better readability
                g.setColour(juce::Colours::black.withAlpha(0.7f));
                g.fillRoundedRectangle(static_cast<float>(x - 5), static_cast<float>(y - 2), 
                                      static_cast<float>(textWidth + 10), 20.0f, 3.0f);
                
                // Draw text
                g.setColour(juce::Colours::orange);
                g.drawText(text, x, y, textWidth, 16, juce::Justification::left);
            }
        }

        bool MixEditorComponent::keyPressed(const juce::KeyPress &key)
        {
            if (!m_node)
                return false;

            // Ctrl+Z for undo
            if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
            {
                spdlog::info("MixEditorComponent: Undo requested");
                
                auto& undoManager = database::theTrackLibrary.getUndoManager();
                const auto mixId = m_node->getMixProjectLoader().getMixId();
                
                if (undoManager.canUndo(mixId))
                {
                    // Check if we're currently playing and store the position
                    bool wasPlaying = false;
                    double playbackPosition = 0.0;
                    if (m_playbackController && m_playbackController->isMixMode() && m_playbackController->isPlaying())
                    {
                        wasPlaying = true;
                        playbackPosition = m_playbackController->getCurrentPositionSeconds();
                        spdlog::debug("[UNDO] Stopping playback before undo operation");
                        m_playbackController->stop();
                    }
                    
                    undoManager.undo(mixId, [this, wasPlaying, playbackPosition]() {
                        // Refresh the mix after undo
                        if (m_node)
                        {
                            spdlog::info("Refreshing mix after undo");
                            m_node->refreshCache(true);  // Force a complete refresh
                            
                            // Use thread-safe lock to ensure mix data is updated atomically
                            if (m_playbackController)
                            {
                                m_playbackController->withMixEngineLock([this, wasPlaying, playbackPosition]() {
                                    // Reload the mix in the playback controller
                                    auto& mixLoader = m_node->getMixProjectLoader();
                                    if (m_playbackController)
                                    {
                                        spdlog::debug("[UNDO] Reloading mix in playback controller");
                                        m_playbackController->loadMix(&mixLoader);
                                    }
                                    
                                    // Refresh the appropriate timeline
                                    if (m_useVirtualTimeline && m_virtualTimeline)
                                    {
                                        m_virtualTimeline->loadMixProject(&mixLoader);
                                    }
                                    else
                                    {
                                        m_timeline.populateFrom(&mixLoader);
                                        m_timeline.repaint();
                                    }
                                    m_viewport.repaint();
                                    
                                    // Resume playback if it was playing (inside the lock)
                                    if (wasPlaying && m_playbackController)
                                    {
                                        spdlog::debug("[UNDO] Resuming playback at position {}", playbackPosition);
                                        m_playbackController->playMixFrom(playbackPosition);
                                    }
                                });
                            }
                            else
                            {
                                // No playback controller, just refresh timeline
                                auto& mixLoader = m_node->getMixProjectLoader();
                                if (m_useVirtualTimeline && m_virtualTimeline)
                                {
                                    m_virtualTimeline->loadMixProject(&mixLoader);
                                }
                                else
                                {
                                    m_timeline.populateFrom(&mixLoader);
                                    m_timeline.repaint();
                                }
                                m_viewport.repaint();
                            }
                        }
                    });
                }
                else
                {
                    spdlog::info("No undo available for mix {}", mixId);
                }
                return true;
            }
            // Ctrl+Y or Cmd+Shift+Z for redo
            else if (key == juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0) ||
                     key == juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
            {
                spdlog::info("MixEditorComponent: Redo requested");
                
                auto& undoManager = database::theTrackLibrary.getUndoManager();
                const auto mixId = m_node->getMixProjectLoader().getMixId();
                
                if (undoManager.canRedo(mixId))
                {
                    // Check if we're currently playing and store the position
                    bool wasPlaying = false;
                    double playbackPosition = 0.0;
                    if (m_playbackController && m_playbackController->isMixMode() && m_playbackController->isPlaying())
                    {
                        wasPlaying = true;
                        playbackPosition = m_playbackController->getCurrentPositionSeconds();
                        spdlog::debug("[REDO] Stopping playback before redo operation");
                        m_playbackController->stop();
                    }
                    
                    undoManager.redo(mixId, [this, wasPlaying, playbackPosition]() {
                        // Refresh the mix after redo
                        if (m_node)
                        {
                            spdlog::info("Refreshing mix after redo");
                            m_node->refreshCache(true);  // Force a complete refresh
                            
                            // Use thread-safe lock to ensure mix data is updated atomically
                            if (m_playbackController)
                            {
                                m_playbackController->withMixEngineLock([this, wasPlaying, playbackPosition]() {
                                    // Reload the mix in the playback controller
                                    auto& mixLoader = m_node->getMixProjectLoader();
                                    if (m_playbackController)
                                    {
                                        spdlog::debug("[REDO] Reloading mix in playback controller");
                                        m_playbackController->loadMix(&mixLoader);
                                    }
                                    
                                    // Refresh the appropriate timeline
                                    if (m_useVirtualTimeline && m_virtualTimeline)
                                    {
                                        m_virtualTimeline->loadMixProject(&mixLoader);
                                    }
                                    else
                                    {
                                        m_timeline.populateFrom(&mixLoader);
                                        m_timeline.repaint();
                                    }
                                    m_viewport.repaint();
                                    
                                    // Resume playback if it was playing (inside the lock)
                                    if (wasPlaying && m_playbackController)
                                    {
                                        spdlog::debug("[REDO] Resuming playback at position {}", playbackPosition);
                                        m_playbackController->playMixFrom(playbackPosition);
                                    }
                                });
                            }
                            else
                            {
                                // No playback controller, just refresh timeline
                                auto& mixLoader = m_node->getMixProjectLoader();
                                if (m_useVirtualTimeline && m_virtualTimeline)
                                {
                                    m_virtualTimeline->loadMixProject(&mixLoader);
                                }
                                else
                                {
                                    m_timeline.populateFrom(&mixLoader);
                                    m_timeline.repaint();
                                }
                                m_viewport.repaint();
                            }
                        }
                    });
                }
                else
                {
                    spdlog::info("No redo available for mix {}", mixId);
                }
                return true;
            }
            
            return false;
        }

        void MixEditorComponent::unloadMix()
        {
            if (m_node)
            {
                m_timeline.releaseMixLoader();
                
                if (m_useVirtualTimeline && m_virtualTimeline)
                {
                    m_virtualTimeline->loadMixProject(nullptr); // Clear the project
                }
                
                m_node->release(REFCOUNT_DEBUG_ARGS);
                m_node = nullptr;
            }
        }

        void MixEditorComponent::loadMix(database::MixNode *node)
        {
            spdlog::info("[MixEditor] loadMix called with node: {}", node ? "valid" : "null");
            
            // Special case: run performance harness if node is null and virtual timeline is enabled
            if (!node && m_useVirtualTimeline && m_virtualTimeline)
            {
                spdlog::info("[MixEditor] Running performance harness with 282 tracks");
                m_virtualTimeline->runPerfHarness(282);
                return;
            }
            
            assert(node != nullptr && "MixNode should not be null in loadMix()");
            if (m_node)
            {
                spdlog::info("[MixEditor] Releasing previous node");
                m_node->release(REFCOUNT_DEBUG_ARGS); // Release previous node if any
            }
            m_node = node; // Take ownership of the new node
            node->retain(REFCOUNT_DEBUG_ARGS); // Retain the node to ensure it stays valid
            node->refreshCache(false);
            
            // Check if mix is exported and set read-only mode
            const auto& mixManager = database::theTrackLibrary.getMixManager();
            auto mixInfo = mixManager.getMix(node->getMixInfo().mixId);
            m_isReadOnly = mixInfo.exportFolder.has_value() && !mixInfo.exportFolder->empty();

            if (m_isReadOnly)
            {
                spdlog::info("[MixEditor] Mix {} is in read-only mode (exported to folder: {})",
                            node->getMixInfo().mixId, mixInfo.exportFolder.value_or(""));
            }
            
            // Configure timeline for read-only mode
            m_timeline.setReadOnly(m_isReadOnly);
            if (m_virtualTimeline)
            {
                // If we add setReadOnly to VirtualTimelineComponent later
                // m_virtualTimeline->setReadOnly(m_isReadOnly);
            }
            
            auto& loader = node->getMixProjectLoader();
            spdlog::info("[MixEditor] Loading mix with {} tracks into timeline", loader.getMixTracks().size());
            
            // Load the mix into the playback controller first
            if (m_playbackController)
            {
                bool loadSuccess = m_playbackController->loadMix(&loader);
                spdlog::info("[MixEditor] Mix loaded into playback controller: {}", loadSuccess ? "success" : "failed");
            }
            else
            {
                spdlog::error("[MixEditor] m_playbackController is null!");
            }
            
            // Check if waveform loading is enabled
            bool preloadWaveforms = config::theSettings.mixEditingSettings.preloadWaveformsOnMixOpen;
            
            if (preloadWaveforms)
            {
                // Collect waveform loading requirements
                auto waveformStatus = collectWaveformRequests(&loader);
                
                // Build WaveformLoadingTask requests
                std::vector<database::background_tasks::WaveformLoadingTask::WaveformRequest> waveformRequests;
                bool hasWaveformsToLoad = false;
                
                for (const auto& [trackId, needsLoading] : waveformStatus)
                {
                    if (const auto* trackInfo = loader.getTrackInfoForId(trackId))
                    {
                        database::background_tasks::WaveformLoadingTask::WaveformRequest req;
                        req.trackId = trackId;
                        req.filePath = trackInfo->reconstructFullPath();
                        req.needsLoading = needsLoading;
                        req.trackName = trackInfo->title;
                        waveformRequests.push_back(req);
                        
                        if (needsLoading)
                            hasWaveformsToLoad = true;
                    }
                }
                
                if (hasWaveformsToLoad)
                {
                    spdlog::info("[MixEditor] Loading waveforms for {} tracks", waveformRequests.size());
                    
                    // Create and launch the waveform loading task
                    auto* task = new database::background_tasks::WaveformLoadingTask(
                        std::move(waveformRequests),
                        m_formatManager,
                        m_thumbnailCache);
                    
                    // Capture loader pointer for completion callback
                    auto* loaderPtr = &loader;
                    
                    TaskDialog::launch(
                        "Loading Waveforms",
                        task,
                        TaskDialog::AutoCloseMode::Immediate,  // Close immediately on success
                        0,  // No delay needed
                        this,
                        [this, loaderPtr, task]() {
                            // After loading completes (or user cancels)
                            spdlog::info("[MixEditor] Waveform loading completed. Success: {}, Failed: {}", 
                                       task->getSuccessCount(), task->getFailedTracks().size());
                            
                            // Now populate the timeline with loaded waveforms
                            populateTimeline(loaderPtr);
                        });
                    
                    task->release(REFCOUNT_DEBUG_ARGS);
                }
                else
                {
                    spdlog::info("[MixEditor] All waveforms already cached, populating timeline immediately");
                    // All waveforms already cached, populate immediately
                    populateTimeline(&loader);
                }
            }
            else
            {
                spdlog::info("[MixEditor] Waveform preloading disabled, populating timeline immediately");
                // Waveform preloading disabled, populate immediately
                populateTimeline(&loader);
            }
        }

        void MixEditorComponent::resized()
        {
            const auto startTime = std::chrono::high_resolution_clock::now();
            
            auto bounds = getLocalBounds();
            
            // Place the marker ruler at the top
            m_markerRuler.setBounds(bounds.removeFromTop(MarkerRulerComponent::RULER_HEIGHT));
            
            // The viewport fills the remaining area below the ruler
            const auto viewportStart = std::chrono::high_resolution_clock::now();
            m_viewport.setBounds(bounds);
            const auto viewportEnd = std::chrono::high_resolution_clock::now();
            
            // Position the playhead overlay to match the viewport's viewed area
            updatePlayheadOverlayPosition();
            
            // Notify the timeline that the viewport has resized
            const auto timelineStart = std::chrono::high_resolution_clock::now();
            if (m_useVirtualTimeline && m_virtualTimeline)
            {
                const auto viewBounds = m_viewport.getViewArea();
                // Use the actual viewport widget bounds for height, not the viewed component's size
                const auto actualViewportBounds = juce::Rectangle<int>(
                    viewBounds.getX(), 
                    viewBounds.getY(),
                    viewBounds.getWidth(),
                    m_viewport.getHeight()  // Use the viewport widget's actual height
                );
                m_virtualTimeline->setViewportBounds(actualViewportBounds);
            }
            else
            {
                m_timeline.viewportResized();
            }
            
            const auto timelineEnd = std::chrono::high_resolution_clock::now();
            
            const auto endTime = std::chrono::high_resolution_clock::now();
            
            const auto viewportDuration = std::chrono::duration_cast<std::chrono::microseconds>(viewportEnd - viewportStart);
            const auto timelineDuration = std::chrono::duration_cast<std::chrono::microseconds>(timelineEnd - timelineStart);
            const auto totalDuration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
            
            spdlog::info("MixEditorComponent::resized - Performance:");
            spdlog::info("  Viewport.setBounds: {} µs", viewportDuration.count());
            spdlog::info("  Timeline.viewportResized: {} µs", timelineDuration.count());
            spdlog::info("  Total: {} µs", totalDuration.count());
        }

        void MixEditorComponent::updateCueAttachInData(int orderInMix, const database::MixTrack& updatedTrack)
        {
            if (!m_node)
            {
                spdlog::error("MixEditorComponent::updateCueAttachInData - No mix node loaded");
                return;
            }

            // Check if mix is read-only
            if (m_isReadOnly)
            {
                showMoveBackDialog();
                return;
            }

            spdlog::info("Updating cue/attach points for track at position {}", orderInMix);
            
            // Get access to the mix tracks
            auto& mixTracks = const_cast<audio::MixProjectLoader&>(m_node->getMixProjectLoader()).getMixTracks();
            
            // Find and update the track by orderInMix AND trackId (to handle duplicate orderInMix)
            for (auto& track : mixTracks)
            {
                // Match both orderInMix AND trackId to handle data corruption cases
                if (track.orderInMix == orderInMix && track.trackId == updatedTrack.trackId)
                {
                    // Check if cue or attach points actually changed
                    const auto cueStartChanged = track.cueStart != updatedTrack.cueStart;
                    const auto attachChanged = track.attachFrom != updatedTrack.attachFrom || 
                                              track.attachTo != updatedTrack.attachTo;
                    
                    track.cueStart = updatedTrack.cueStart;
                    track.cueEnd = updatedTrack.cueEnd;
                    track.attachFrom = updatedTrack.attachFrom;
                    track.attachTo = updatedTrack.attachTo;
                    spdlog::info("Updated cue/attach points for track {} at position {}", track.trackId, orderInMix);
                    
                    // Save changes
                    saveMixChanges();
                    
                    // Tell timeline to reposition this specific track
                    m_timeline.repositionTrack(track.trackId);
                    
                    if (m_useVirtualTimeline && m_virtualTimeline)
                    {
                        auto& mixLoader = m_node->getMixProjectLoader();
                        m_virtualTimeline->loadMixProject(&mixLoader);
                    }
                    
                    // Tell playback controller to reload if positions changed
                    // Note: cueStart of first track affects global offset, attach points affect all positions
                    const auto isFirstTrack = (track.orderInMix == 0);
                    const auto needsRecalc = attachChanged || (isFirstTrack && cueStartChanged);
                    
                    if (needsRecalc && m_playbackController && m_playbackController->isMixMode())
                    {
                        spdlog::info("Reloading mix after {} change",
                                    attachChanged ? "attach point" : "first track cueStart");
                        // Reload the mix to update positions
                        m_playbackController->loadMix(&m_node->getMixProjectLoader());
                    }
                    break;
                }
            }
        }
        
        void MixEditorComponent::updateCuePointsInData(int orderInMix, jucyaudio::Duration_t cueStart, jucyaudio::Duration_t cueEnd)
        {
            if (!m_node)
            {
                spdlog::error("MixEditorComponent::updateCuePointsInData - No mix node loaded");
                return;
            }
            
            spdlog::info("Updating cue points for track at position {}: cueStart={}, cueEnd={}", 
                        orderInMix, cueStart.count(), cueEnd.count());
            
            // Get access to the mix tracks
            auto& mixTracks = const_cast<audio::MixProjectLoader&>(m_node->getMixProjectLoader()).getMixTracks();
            
            // Find and update the track by orderInMix
            for (auto& track : mixTracks)
            {
                if (track.orderInMix == orderInMix)
                {
                    track.cueStart = cueStart;
                    track.cueEnd = cueEnd;
                    
                    // Save changes to database
                    if (theTrackLibrary.getMixManager().updateMixTrack(m_node->getMixProjectLoader().getMixId(), track))
                    {
                        spdlog::info("Successfully updated cue points in database for track {}", track.trackId);
                    }
                    else
                    {
                        spdlog::error("Failed to update cue points in database for track {}", track.trackId);
                    }
                    
                    break;
                }
            }
        }

        void MixEditorComponent::updateEnvelopeInData(int orderInMix, const std::vector<database::EnvelopePoint>& points)
        {
            if (!m_node)
            {
                spdlog::error("MixEditorComponent::updateEnvelopeInData - No mix node loaded");
                return;
            }

            // Check if mix is read-only
            if (m_isReadOnly)
            {
                showMoveBackDialog();
                return;
            }

            spdlog::info("Updating envelope for track at position {} with {} points", orderInMix, points.size());
            
            // Use thread-safe locking when modifying envelope points that the audio thread reads
            if (m_playbackController)
            {
                m_playbackController->withMixEngineLock([&]()
                {
                    // Get access to the mix tracks
                    auto& mixTracks = const_cast<audio::MixProjectLoader&>(m_node->getMixProjectLoader()).getMixTracks();
                    
                    // Find and update the track by orderInMix (and trackId if available)
                    for (auto& track : mixTracks)
                    {
                        if (track.orderInMix == orderInMix)
                        {
                            track.envelopePoints = points;
                            spdlog::info("Updated envelope points for track {} at position {}", track.trackId, orderInMix);
                            
                            // Save changes
                            saveMixChanges();
                            break;
                        }
                    }
                });
            }
            else
            {
                spdlog::error("MixEditorComponent::updateEnvelopeInData - No playback controller available");
            }
        }
        
        void MixEditorComponent::updateGainAdjustmentInData(int orderInMix, float newGain, bool saveToDatabase)
        {
            if (!m_node)
            {
                spdlog::error("MixEditorComponent::updateGainAdjustmentInData - No mix node loaded");
                return;
            }
            
            spdlog::info("Updating gain adjustment for track at position {} to {} (save: {})", 
                        orderInMix, newGain, saveToDatabase);
            
            // Use thread-safe locking when modifying gain that the audio thread reads
            if (m_playbackController)
            {
                m_playbackController->withMixEngineLock([&]()
                {
                    // Get access to the mix tracks
                    auto& mixTracks = const_cast<audio::MixProjectLoader&>(m_node->getMixProjectLoader()).getMixTracks();
                    
                    // Find and update the track by orderInMix
                    for (auto& track : mixTracks)
                    {
                        if (track.orderInMix == orderInMix)
                        {
                            track.gainAdjustment = newGain;
                            spdlog::info("Updated gain adjustment for track {} at position {} to {}", 
                                       track.trackId, orderInMix, newGain);
                            
                            // Only save to database if requested (i.e., when OK is clicked)
                            if (saveToDatabase)
                            {
                                saveMixChanges();
                                
                                // Update the virtual timeline's internal data after saving
                                if (m_virtualTimeline)
                                {
                                    auto& mixLoader = m_node->getMixProjectLoader();
                                    m_virtualTimeline->loadMixProject(&mixLoader);
                                }
                            }
                            // For preview (dragging), we just need to update the playback engine
                            // The change is already applied above by modifying track.gainAdjustment
                            
                            break;
                        }
                    }
                });
            }
            else
            {
                spdlog::error("MixEditorComponent::updateGainAdjustmentInData - No playback controller available");
            }
        }

        void MixEditorComponent::saveMixChanges()
        {
            if (!m_node)
            {
                spdlog::error("MixEditorComponent::saveMixChanges - No mix node loaded");
                return;
            }
            spdlog::info("Saving mix changes to database");

            // Get the current mix info and tracks
            auto &mixProjectLoader = m_node->getMixProjectLoader();
            //auto mixId = mixProjectLoader.getMixId();
            auto &mixTracks = mixProjectLoader.getMixTracks();

            // Get mix info from database
            auto &mixInfo = mixProjectLoader.getMixInfo();
            mixInfo.totalDuration = mixProjectLoader.calculateMixDuration();

            // Save changes back to database
            spdlog::info("About to call createOrUpdateMix for mix {} with {} tracks", mixInfo.mixId, mixTracks.size());
            if (database::theTrackLibrary.getMixManager().createOrUpdateMix(mixInfo, mixTracks))
            {
                spdlog::info("Successfully saved mix changes");
            }
            else
            {
                spdlog::error("Failed to save mix changes");
            }
        }
        
        void MixEditorComponent::handleMixPlayback(double startTime, bool alwaysPlay)
        {
            spdlog::info("[MixEditor] handleMixPlayback called - startTime: {}, alwaysPlay: {}", startTime, alwaysPlay);
            
            // Special case: negative value means stop
            if (startTime < 0)
            {
                if (m_playbackController && m_playbackController->isPlaying())
                {
                    spdlog::info("[MixEditor] Stopping playback (negative startTime)");
                    m_playbackController->stop();
                }
                return;
            }
            
            if (!m_node)
            {
                spdlog::error("[MixEditor] No mix node loaded");
                return;
            }
            
            if (!m_playbackController)
            {
                spdlog::error("[MixEditor] m_playbackController is null!");
                return;
            }
            
            // Check if we should toggle playback (for keyboard shortcuts)
            if (!alwaysPlay && m_playbackController->isPlaying())
            {
                // We're playing and not forced to play - pause/stop
                spdlog::info("[MixEditor] Pausing playback");
                m_playbackController->pause();
                return;
            }
            
            // Ensure mix is loaded
            if (!m_playbackController->isMixMode())
            {
                spdlog::info("[MixEditor] Loading mix into playback controller");
                if (!m_playbackController->loadMix(&m_node->getMixProjectLoader()))
                {
                    spdlog::error("[MixEditor] Failed to load mix");
                    return;
                }
            }
            
            // Play from the specified position  
            spdlog::info("[MixEditor] Starting playback at {:.2f}s", startTime);
            m_playbackController->playMixFrom(startTime);
        }
        
        void MixEditorComponent::handleDeleteSelectedTrack()
        {
            spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Entry");
            
            // Get the track ID to remove - handle both timeline implementations
            int trackIdToRemove = -1;
            
            if (m_useVirtualTimeline && m_virtualTimeline)
            {
                const auto selectedTracks = m_virtualTimeline->getSelectedDatabaseTrackIds();
                if (selectedTracks.empty())
                {
                    spdlog::warn("handleDeleteSelectedTrack called but no track selected (virtual timeline).");
                    spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> No track selected, exiting.");
                    return;
                }
                trackIdToRemove = selectedTracks[0]; // For now, delete the first selected track
            }
            else
            {
                auto* selectedTrackComponent = m_timeline.getSelectedTrack();
                if (!selectedTrackComponent)
                {
                    spdlog::warn("handleDeleteSelectedTrack called but no track selected.");
                    spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> No track selected, exiting.");
                    return;
                }
                trackIdToRemove = selectedTrackComponent->getTrackId();
            }
            
            if (!m_node)
            {
                spdlog::error("handleDeleteSelectedTrack called but no mix node loaded.");
                spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> No mix node loaded, exiting.");
                return;
            }
            
            auto& mixLoader = m_node->getMixProjectLoader();
            const auto mixId = mixLoader.getMixId();
            spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Attempting to delete track {} from mix {}", trackIdToRemove, mixId);

            // 2. Quick settings check - no dialog if not configured to ask
            bool shouldRemoveFromWorkingSet = false;
            
            if (config::theSettings.mixEditingSettings.removeFromWorkingSetOnDelete.get())
            {
                // Only check if we need to ask - avoid unnecessary work
                if (config::theSettings.mixEditingSettings.askBeforeRemovingFromWorkingSet.get())
                {
                    const auto& mixTracks = mixLoader.getMixTracks();
                    int trackOccurrences = 0;
                    for (const auto& mixTrack : mixTracks)
                    {
                        if (mixTrack.trackId == trackIdToRemove)
                        {
                            trackOccurrences++;
                            if (trackOccurrences > 1) break; // No need to count more
                        }
                    }
                    
                    if (trackOccurrences == 1)
                    {
                        const auto& mixInfo = mixLoader.getMixInfo();
                        if (mixInfo.source_ws_id > 0)
                        {
                            auto usersChoice{config::theSettings.mixEditingSettings.removeTrackOption.get().value};
                            if(usersChoice == config::RemoveTrackOption::AskUser)
                            {
                                const auto result = juce::AlertWindow::showYesNoCancelBox(
                                    juce::AlertWindow::QuestionIcon,
                                    "Remove Track",
                                    "Remove this track from the mix?\n\nAlso remove from the source working set?",
                                    "Remove from Both",
                                "Remove from Mix Only",
                                "Cancel");

                                if(result == 0)
                                {
                                    usersChoice = config::RemoveTrackOption::CancelOperation;
                                }
                                else if(result == 1)
                                {
                                    usersChoice = config::RemoveTrackOption::RemoveFromMixOnly;
                                }
                                else
                                {
                                    usersChoice = config::RemoveTrackOption::RemoveFromBoth;
                                }
                            }
                            if(usersChoice == config::RemoveTrackOption::CancelOperation)
                            {
                                spdlog::info("User cancelled track removal.");
                                return;
                            }
                            else if(usersChoice == config::RemoveTrackOption::RemoveFromBoth)
                            {
                                shouldRemoveFromWorkingSet = true;
                            }
                        }
                    }
                }
                else
                {
                    // Auto-remove from working set without asking
                    const auto& mixInfo = mixLoader.getMixInfo();
                    if (mixInfo.source_ws_id > 0)
                    {
                        shouldRemoveFromWorkingSet = true;
                    }
                }
            }
            // Skip confirmation dialog if not configured to ask
            else if (config::theSettings.mixEditingSettings.askBeforeRemovingFromWorkingSet.get())
            {
                // Only show confirmation if configured to ask
                const auto result = juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon,
                                                                       "Delete Track",
                                                                       "Are you sure you want to remove this track from the mix?",
                                                                       "Delete", "Cancel");
                if (result == 0) // User cancelled
                {
                    spdlog::info("User cancelled track removal.");
                    return;
                }
            }
            
            // 3. Remember if we were playing and stop playback properly
            const bool wasPlaying = m_playbackController && m_playbackController->isPlaying();
            double playbackPosition = 0.0;
            
            if (wasPlaying)
            {
                // Remember position before stopping
                playbackPosition = m_playbackController->getCurrentPositionSeconds();
                spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Was playing at position {}. Stopping playback.", playbackPosition);
                m_playbackController->stop();
                
                // Remove this sleep - not needed
                // juce::Thread::sleep(50);
            }
            
            // 4. Perform DB Deletions
            spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Removing track from DB.");
            if (!database::theTrackLibrary.getMixManager().removeTrackFromMix(mixId, trackIdToRemove))
            {
                spdlog::error("Failed to remove track {} from mix {}", trackIdToRemove, mixId);
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", "Failed to remove track from database.");
                return;
            }
            
            if (shouldRemoveFromWorkingSet)
            {
                const auto& mixInfo = mixLoader.getMixInfo();
                const WorkingSetId wsId = mixInfo.source_ws_id;
                if (database::theTrackLibrary.getWorkingSetManager().removeTrackFromWorkingSet(wsId, trackIdToRemove))
                {
                    spdlog::info("Also removed track {} from working set {}.", trackIdToRemove, wsId);
                }
                else
                {
                    spdlog::warn("Failed to remove track {} from working set {}.", trackIdToRemove, wsId);
                }
            }
            
            // 5. Reload data model from DB
            spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Reloading mix loader from DB.");
            if (!mixLoader.reloadFromDatabase())
            {
                spdlog::critical("CRITICAL: Failed to reload mix loader from DB after deletion! The application state is now inconsistent.");
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Critical Error", "Failed to reload mix data after deletion. Please restart the application.");
                return;
            }
            
            // 6. Refresh the timeline UI first (but only if not playing - otherwise do it after)
            if (!wasPlaying)
            {
                spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Refreshing timeline UI.");
                m_timeline.refreshAfterDeletion(trackIdToRemove);
                
                if (m_useVirtualTimeline && m_virtualTimeline)
                {
                    m_virtualTimeline->loadMixProject(&mixLoader);
                }
            }
            
            // 7. Re-load mix in playback controller only if needed
            if (m_playbackController && wasPlaying)
            {
                spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Reloading mix in playback controller.");
                bool loadSuccess = m_playbackController->loadMix(&mixLoader);
                spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Playback controller reload success: {}", loadSuccess);
                
                if (!loadSuccess)
                {
                    spdlog::error("Failed to reload mix after deletion!");
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                        "Error", "Failed to reload mix after track deletion.");
                    return;
                }
                
                // Resume playback immediately
                spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Resuming playback from position {}", playbackPosition);
                m_playbackController->playMixFrom(playbackPosition);
                
                // Now refresh the UI after playback has resumed
                spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Refreshing timeline UI after playback resume.");
                m_timeline.refreshAfterDeletion(trackIdToRemove);
                
                if (m_useVirtualTimeline && m_virtualTimeline)
                {
                    m_virtualTimeline->loadMixProject(&mixLoader);
                }
            }
            
            // 8. Ready for playback
            spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Exit");
        }
        
        void MixEditorComponent::handlePasteTracks(const std::vector<database::MixTrack>& tracks, int position, bool before)
        {
            spdlog::info("[PASTE] handlePasteTracks called with {} tracks, position={}, before={}", 
                        tracks.size(), position, before);
            
            if (!m_node || tracks.empty())
            {
                spdlog::warn("[PASTE] Aborting paste: m_node={}, tracks.empty()={}", 
                            m_node != nullptr, tracks.empty());
                return;
            }
            
            // Stop playback to avoid race conditions when modifying tracks
            const bool wasPlaying = m_playbackController && m_playbackController->isPlaying();
            double playbackPosition = 0.0;
            
            if (wasPlaying)
            {
                playbackPosition = m_playbackController->getCurrentPositionSeconds();
                spdlog::debug("[PASTE] Stopping playback before paste operation");
                m_playbackController->stop();
            }
            
            spdlog::info("[PASTE] Pasting {} tracks at position {}, before={}", tracks.size(), position, before);
            
            auto& mixLoader = m_node->getMixProjectLoader();
            auto& currentTracks = mixLoader.getMixTracks();
            
            spdlog::info("[PASTE] Current mix has {} tracks", currentTracks.size());
            
            // Create a new vector with all tracks
            std::vector<database::MixTrack> newTracks;
            newTracks.reserve(currentTracks.size() + tracks.size());
            
            // Calculate the insertion position  
            // position is the orderInMix of the selected track, not an array index
            const int insertPosition = before ? position : position + 1;
            spdlog::info("[PASTE] Will insert at orderInMix position {}", insertPosition);
            
            // Copy tracks before the insertion point
            for (const auto& track : currentTracks)
            {
                if (track.orderInMix < insertPosition)
                {
                    database::MixTrack newTrack = track;
                    newTrack.orderInMix = static_cast<int>(newTracks.size());
                    newTracks.push_back(newTrack);
                    spdlog::info("[PASTE] Copied track {} to position {}", track.trackId, newTrack.orderInMix);
                }
            }
            
            // Insert the pasted tracks with new orderInMix values
            for (const auto& track : tracks)
            {
                database::MixTrack newTrack = track;
                newTrack.orderInMix = static_cast<int>(newTracks.size());
                // Keep the same trackId as we're duplicating the same track
                newTracks.push_back(newTrack);
                spdlog::info("[PASTE] Inserted pasted track {} at position {}", newTrack.trackId, newTrack.orderInMix);
            }
            
            // Copy remaining tracks after the insertion point, renumbering them
            for (const auto& track : currentTracks)
            {
                if (track.orderInMix >= insertPosition)
                {
                    database::MixTrack adjustedTrack = track;
                    adjustedTrack.orderInMix = static_cast<int>(newTracks.size());
                    newTracks.push_back(adjustedTrack);
                    spdlog::info("[PASTE] Moved track {} to position {}", track.trackId, adjustedTrack.orderInMix);
                }
            }
            
            // Store the original count before we move
            const auto originalTrackCount = currentTracks.size();
            const auto expectedNewCount = originalTrackCount + tracks.size();
            
            // Replace the mix tracks with our new ordering
            currentTracks = std::move(newTracks);
            
            // Verify the move was successful
            spdlog::info("[PASTE_DB] After move: currentTracks.size() = {}, expected = {}", 
                        currentTracks.size(), expectedNewCount);
            
            // Save to database
            spdlog::info("[PASTE_DB] Attempting to save mix after paste...");
            spdlog::info("[PASTE_DB] Mix now has {} tracks (was {} before paste)", 
                        currentTracks.size(), originalTrackCount);
            
            // Log the actual tracks being saved
            spdlog::info("[PASTE_DB] Tracks to save:");
            for (const auto& track : currentTracks)
            {
                spdlog::info("[PASTE_DB]   - Track {} at position {}", track.trackId, track.orderInMix);
            }
            
            if (mixLoader.saveMix(theTrackLibrary.getMixManager()))
            {
                spdlog::info("[PASTE_DB] Successfully saved mix with {} tracks to database", currentTracks.size());
                spdlog::info("[PASTE_DB] Successfully pasted {} tracks at position {}", tracks.size(), insertPosition);
                
                // Reload from database to ensure consistency
                spdlog::info("[PASTE_DB] Reloading mix from database...");
                mixLoader.reloadFromDatabase();
                spdlog::info("[PASTE_DB] Mix reloaded, now has {} tracks", mixLoader.getMixTracks().size());
                
                // Reload in playback controller if it was playing
                if (m_playbackController && wasPlaying)
                {
                    spdlog::debug("[PASTE] Reloading mix in playback controller");
                    m_playbackController->loadMix(&mixLoader);
                }
                
                // Refresh the timeline
                if (m_useVirtualTimeline && m_virtualTimeline)
                {
                    spdlog::info("[PASTE_DB] Refreshing virtual timeline with updated mix");
                    m_virtualTimeline->loadMixProject(&mixLoader);
                }
                else
                {
                    spdlog::info("[PASTE_DB] Refreshing regular timeline with updated mix");
                    m_timeline.populateFrom(&mixLoader);
                }
                
                // Resume playback if it was playing
                if (wasPlaying)
                {
                    spdlog::debug("[PASTE] Resuming playback at position {}", playbackPosition);
                    m_playbackController->playMixFrom(playbackPosition);
                }
                spdlog::info("[PASTE_DB] Timeline refresh complete");
            }
            else
            {
                spdlog::error("[PASTE_DB] Failed to save mix after pasting tracks - database operation failed");
            }
        }
        
        void MixEditorComponent::handleRemoveFollowingTracks(int afterOrder)
        {
            if (!m_node)
                return;
            
            spdlog::info("Removing all tracks after orderInMix {}", afterOrder);
            
            // Stop playback to avoid race conditions when modifying tracks
            const bool wasPlaying = m_playbackController && m_playbackController->isPlaying();
            double playbackPosition = 0.0;
            
            if (wasPlaying)
            {
                playbackPosition = m_playbackController->getCurrentPositionSeconds();
                spdlog::debug("[REMOVE] Stopping playback before removing tracks");
                m_playbackController->stop();
            }
            
            auto& mixLoader = m_node->getMixProjectLoader();
            auto& mixTracks = mixLoader.getMixTracks();
            
            // Count tracks to remove
            int tracksToRemove = 0;
            for (const auto& track : mixTracks)
            {
                if (track.orderInMix > afterOrder)
                {
                    tracksToRemove++;
                }
            }
            
            if (tracksToRemove == 0)
            {
                spdlog::info("No tracks to remove after position {}", afterOrder);
                return;
            }
            
            // Confirm with user
            const auto result = juce::AlertWindow::showOkCancelBox(
                juce::AlertWindow::QuestionIcon,
                "Remove Following Tracks",
                juce::String::formatted("Remove %d tracks after this position?", tracksToRemove),
                "Remove",
                "Cancel");
            
            if (!result)
                return;
            
            // Create new track list with only tracks up to and including afterOrder
            std::vector<database::MixTrack> newTracks;
            for (const auto& track : mixTracks)
            {
                if (track.orderInMix <= afterOrder)
                {
                    newTracks.push_back(track);
                }
            }
            
            // Replace the mix tracks
            mixTracks = std::move(newTracks);
            
            // Save to database
            if (mixLoader.saveMix(theTrackLibrary.getMixManager()))
            {
                spdlog::info("Successfully removed {} tracks after position {}", tracksToRemove, afterOrder);
                
                // Reload from database to ensure consistency
                mixLoader.reloadFromDatabase();
                
                // Reload in playback controller if it was playing
                if (m_playbackController && wasPlaying)
                {
                    spdlog::debug("[REMOVE] Reloading mix in playback controller");
                    m_playbackController->loadMix(&mixLoader);
                }
                
                // Refresh the timeline
                if (m_useVirtualTimeline && m_virtualTimeline)
                {
                    m_virtualTimeline->loadMixProject(&mixLoader);
                }
                else
                {
                    m_timeline.populateFrom(&mixLoader);
                }
                
                // Resume playback if it was playing
                if (wasPlaying)
                {
                    spdlog::debug("[REMOVE] Resuming playback at position {}", playbackPosition);
                    m_playbackController->playMixFrom(playbackPosition);
                }
            }
            else
            {
                spdlog::error("Failed to save mix after removing tracks");
            }
        }
        
        void MixEditorComponent::updatePlayhead()
        {
            if (m_playbackController && m_playbackController->isMixMode())
            {
                if (m_playbackController->isPlaying())
                {
                    // Skip updates if we're actively scrolling or just finished scrolling
                    const auto currentTime = juce::Time::currentTimeMillis();
                    const auto timeSinceScroll = currentTime - m_lastScrollTime;
                    if (timeSinceScroll < SCROLL_PAUSE_DURATION_MS)
                    {
                        // Still in the scroll pause period, skip this update
                        return;
                    }
                    
                    const double positionSeconds = m_playbackController->getCurrentPositionSeconds();
                    
                    // Calculate playhead position in pixels
                    const double pixelsPerSecond = m_timeline.getPixelsPerSecond();
                    const double playheadPixelPosition = positionSeconds * pixelsPerSecond;
                    
                    // Get the current viewport bounds
                    const auto viewPos = m_viewport.getViewPosition();
                    const auto viewWidth = m_viewport.getViewWidth();
                    const auto visibleRangeStart = viewPos.x;
                    const auto visibleRangeEnd = viewPos.x + viewWidth;
                    
                    // Only update if playhead is visible (with small margin for edge cases)
                    const int margin = 100; // pixels margin to keep updating near edges
                    const bool isPlayheadVisible = playheadPixelPosition >= (visibleRangeStart - margin) && 
                                                  playheadPixelPosition <= (visibleRangeEnd + margin);
                    
                    if (isPlayheadVisible)
                    {
                        // Update playhead position - at 30Hz for balanced performance/smoothness
                        if (m_useVirtualTimeline && m_virtualTimeline)
                        {
                            m_virtualTimeline->setPlayheadPosition(positionSeconds);
                        }
                        else
                        {
                            m_playheadOverlay.setPlayheadPosition(positionSeconds, pixelsPerSecond);
                        }
                        m_markerRuler.setPlaybackPosition(positionSeconds * 1000.0);
                    }
                    // If playhead is not visible, skip the expensive repaint operations
                    
                    // Check if we've reached the end
                    const double totalSeconds = m_playbackController->getLengthInSeconds();
                    if (totalSeconds > 0 && positionSeconds >= totalSeconds)
                    {
                        spdlog::info("Playback reached end of mix");
                        m_playbackController->stop();
                        if (m_useVirtualTimeline && m_virtualTimeline)
                        {
                            m_virtualTimeline->setPlayheadPosition(-1.0);
                        }
                        else
                        {
                            m_playheadOverlay.setPlayheadPosition(-1.0, pixelsPerSecond);
                        }
                    }
                }
                else
                {
                    // Not playing - hide playhead
                    if (m_useVirtualTimeline && m_virtualTimeline)
                    {
                        m_virtualTimeline->setPlayheadPosition(-1.0);
                    }
                    else
                    {
                        m_playheadOverlay.setPlayheadPosition(-1.0, m_timeline.getPixelsPerSecond());
                    }
                }
            }
            else
            {
                // Not in mix mode - hide playhead
                if (m_useVirtualTimeline && m_virtualTimeline)
                {
                    m_virtualTimeline->setPlayheadPosition(-1.0);
                }
                else
                {
                    m_playheadOverlay.setPlayheadPosition(-1.0, m_timeline.getPixelsPerSecond());
                }
            }
        }
        
        void MixEditorComponent::loadMixMarkers()
        {
            if (!m_node)
                return;
                
            const auto mixId = m_node->getMixProjectLoader().getMixId();
            const auto& mixMarkerManager = database::theTrackLibrary.getTrackDatabase()->getMixMarkerManager();
            const auto markers = mixMarkerManager.getMarkersForMix(mixId);
            
            m_markerRuler.setMarkers(markers);
            spdlog::info("Loaded {} markers for mix {}", markers.size(), mixId);
        }
        
        void MixEditorComponent::scrollBarMoved(juce::ScrollBar* /*scrollBar*/, double /*newRangeStart*/)
        {
            // Track that we're scrolling
            m_lastScrollTime = juce::Time::currentTimeMillis();
            updatePlayheadOverlayPosition();
        }
        
        void MixEditorComponent::updatePlayheadOverlayPosition()
        {
            // When viewport scrolls or changes, update the playhead overlay position
            // The overlay needs to match the timeline's position within the viewport
            const auto viewPos = m_viewport.getViewPosition();
            m_playheadOverlay.setTopLeftPosition(-viewPos.x, -viewPos.y);
            m_playheadOverlay.setSize(m_timeline.getWidth(), m_viewport.getViewHeight());
        }
        
        void MixEditorComponent::saveMixMarker(const database::MixMarker& marker)
        {
            auto& mixMarkerManager = database::theTrackLibrary.getTrackDatabase()->getMixMarkerManager();
            const auto result = marker.marker_id == 0 ? 
                mixMarkerManager.addMarker(marker) : 
                mixMarkerManager.updateMarker(marker);
                
            if (result.isOk())
            {
                loadMixMarkers(); // Reload to refresh display
            }
            else
            {
                spdlog::error("Failed to save mix marker: {}", result.errorMessage);
            }
        }
        
        void MixEditorComponent::handleMarkerClick(MarkerId markerId)
        {
            const auto& mixMarkerManager = database::theTrackLibrary.getTrackDatabase()->getMixMarkerManager();
            const auto marker = mixMarkerManager.getMarker(markerId);
            
            if (!marker.has_value())
            {
                spdlog::error("Marker {} not found", markerId);
                return;
            }
            
            // Show dialog to edit or delete marker
            auto* dialog = new juce::AlertWindow{"Edit Marker", 
                                                 "Edit or delete this marker", 
                                                 juce::AlertWindow::NoIcon};
            
            dialog->addTextEditor("comment", marker->comment, "Comment:");
            dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
            dialog->addButton("Delete", 2);
            dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            
            dialog->enterModalState(true, juce::ModalCallbackFunction::create(
                [this, dialog, markerId](int result)
                {
                    if (result == 1) // Save
                    {
                        const auto& mixMarkerManager = database::theTrackLibrary.getTrackDatabase()->getMixMarkerManager();
                        auto marker = mixMarkerManager.getMarker(markerId);
                        if (marker.has_value())
                        {
                            marker->comment = dialog->getTextEditorContents("comment").toStdString();
                            saveMixMarker(marker.value());
                        }
                    }
                    else if (result == 2) // Delete
                    {
                        auto& mixMarkerManager = database::theTrackLibrary.getTrackDatabase()->getMixMarkerManager();
                        const auto deleteResult = mixMarkerManager.deleteMarker(markerId);
                        if (deleteResult.isOk())
                        {
                            loadMixMarkers(); // Reload to refresh display
                        }
                    }
                    delete dialog;
                }));
        }
        
        void MixEditorComponent::handleMarkerAdd(std::chrono::milliseconds position)
        {
            if (!m_node)
                return;
                
            // Show dialog to get marker comment
            auto* dialog = new juce::AlertWindow{"Add Marker", 
                                                 juce::String::formatted("Add marker at %d:%02d.%03d", 
                                                                        position.count() / 60000,
                                                                        (position.count() % 60000) / 1000,
                                                                        position.count() % 1000),
                                                 juce::AlertWindow::NoIcon};
            
            dialog->addTextEditor("comment", "", "Comment:");
            dialog->addButton("Add", 1, juce::KeyPress(juce::KeyPress::returnKey));
            dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            
            dialog->enterModalState(true, juce::ModalCallbackFunction::create(
                [this, dialog, position](int result)
                {
                    if (result == 1) // Add
                    {
                        const auto comment = dialog->getTextEditorContents("comment").toStdString();
                        if (!comment.empty())
                        {
                            database::MixMarker marker;
                            marker.marker_id = 0; // New marker
                            marker.mix_id = m_node->getMixProjectLoader().getMixId();
                            marker.position = position;
                            marker.comment = comment;
                            saveMixMarker(marker);
                        }
                    }
                    delete dialog;
                }));
        }

        void MixEditorComponent::showMoveBackDialog()
        {
            if (!m_node)
                return;

            const auto& mixInfo = m_node->getMixInfo();
            const auto exportFolder = mixInfo.exportFolder.value_or("");

            auto result = juce::AlertWindow::showOkCancelBox(
                juce::AlertWindow::InfoIcon,
                "Mix is Read-Only",
                juce::String::formatted("This mix was exported to the '%s' folder and is now read-only.\n\n"
                                       "To edit this mix, you need to move it back to the Mixes folder.\n\n"
                                       "Move mix back to Mixes for editing?",
                                       exportFolder.c_str()),
                "Move Back",
                "Cancel");

            if (result)
            {
                auto& mixManager = database::theTrackLibrary.getMixManager();
                if (mixManager.moveBackToMixes(mixInfo.mixId))
                {
                    // Refresh the node to update its status
                    m_node->refreshCache(false);

                    // Reload the mix to update read-only status
                    loadMix(m_node);

                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::InfoIcon,
                        "Mix Moved",
                        "The mix has been moved back to Mixes and can now be edited.");
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon,
                        "Move Failed",
                        "Failed to move the mix back to Mixes folder.");
                }
            }
        }

        void MixEditorComponent::setupVirtualTimeline()
        {
            spdlog::info("Setting up virtual timeline component");
            
            // Create the virtual timeline with required dependencies
            m_virtualTimeline = std::make_unique<VirtualTimelineComponent>(m_formatManager, m_thumbnailCache);
            
            // Wire up callbacks for database updates
            m_virtualTimeline->onCueAttachChanged = [this](int orderInMix, const database::MixTrack& updatedTrack)
            {
                updateCueAttachInData(orderInMix, updatedTrack);
            };
            
            m_virtualTimeline->onEnvelopeChanged = [this](int orderInMix, const std::vector<database::EnvelopePoint>& points)
            {
                updateEnvelopeInData(orderInMix, points);
            };
            
            m_virtualTimeline->onCuePointsChanged = [this](int orderInMix, jucyaudio::Duration_t cueStart, jucyaudio::Duration_t cueEnd)
            {
                updateCuePointsInData(orderInMix, cueStart, cueEnd);
            };
            
            // Wire up playback callbacks
            m_virtualTimeline->onMixPlaybackRequested = [this](double startTime)
            {
                handleMixPlayback(startTime, false);  // Toggle play/pause
            };
            
            m_virtualTimeline->onMixPlaybackAlwaysRequested = [this](double startTime)
            {
                handleMixPlayback(startTime, true);  // Always play
            };
            
            m_virtualTimeline->onSeekRequested = [this](double timePosition)
            {
                if (m_playbackController)
                {
                    m_playbackController->seek(timePosition);
                }
            };
            
            m_virtualTimeline->onDeleteTracksRequested = [this]()
            {
                handleDeleteSelectedTrack();
            };
            
            m_virtualTimeline->onPasteTracksRequested = [this](const std::vector<database::MixTrack>& tracks, int position, bool before)
            {
                handlePasteTracks(tracks, position, before);
            };
            
            m_virtualTimeline->onRemoveFollowingTracksRequested = [this](int afterOrder)
            {
                handleRemoveFollowingTracks(afterOrder);
            };
            
            m_virtualTimeline->onGainAdjustmentChanged = [this](int orderInMix, float newGain, bool saveToDatabase)
            {
                updateGainAdjustmentInData(orderInMix, newGain, saveToDatabase);
            };
        }
        
        std::vector<std::pair<int, bool>> MixEditorComponent::collectWaveformRequests(audio::MixProjectLoader* loader)
        {
            std::vector<std::pair<int, bool>> requests;
            
            if (!loader)
                return requests;
                
            for (const auto& mixTrack : loader->getMixTracks())
            {
                if (const auto* trackInfo = loader->getTrackInfoForId(mixTrack.trackId))
                {
                    // Check if already cached
                    std::vector<unsigned char> cachedData;
                    bool needsLoading = !database::theTrackLibrary.loadWaveform(trackInfo->trackId, cachedData).isOk() 
                                     || cachedData.empty();
                    
                    requests.push_back({static_cast<int>(trackInfo->trackId), needsLoading});
                }
            }
            
            return requests;
        }
        
        void MixEditorComponent::populateTimeline(audio::MixProjectLoader* loader)
        {
            if (!loader)
                return;
                
            // Load the mix into the correct timeline
            if (m_useVirtualTimeline && m_virtualTimeline)
            {
                m_virtualTimeline->loadMixProject(loader);
                // Set initial viewport bounds to ensure tiles are generated
                const auto viewBounds = m_viewport.getViewArea();
                if (!viewBounds.isEmpty())
                {
                    m_virtualTimeline->setViewportBounds(viewBounds);
                }
            }
            else
            {
                m_timeline.populateFrom(loader);
            }
            
            // Calculate mix duration and set it on the ruler
            const auto mixDuration = loader->calculateMixDuration();
            m_markerRuler.setMixDuration(mixDuration);
            
            // Load markers for this mix
            loadMixMarkers();
            
            // Ensure timeline has keyboard focus for playback controls
            if (m_useVirtualTimeline && m_virtualTimeline)
            {
                m_virtualTimeline->grabKeyboardFocus();
            }
            else
            {
                m_timeline.grabKeyboardFocus();
            }
        }
        
    } // namespace ui
} // namespace jucyaudio