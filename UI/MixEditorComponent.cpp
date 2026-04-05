#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/IMixMarkerManager.h>
#include <Database/BackgroundTasks/WaveformLoadingTask.h>
#include <UI/MainComponent.h>
#include <UI/MixEditorComponent.h>
#include <UI/PlaybackController.h>
#include <UI/Settings.h>
#include <UI/TaskDialog.h>
#include <Utils/StringWriter.h>
#include <Utils/UiUtils.h>
#include <Database/TrackLibrary.h>
#include <Database/UndoManager.h>
#include <format>

namespace jucyaudio
{
    namespace ui
    {
        MixEditorComponent::MixEditorComponent()
            : m_timeline{m_formatManager, m_thumbnailCache}
        {
            m_formatManager.registerBasicFormats();
            setWantsKeyboardFocus(true);

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
            m_viewport.setViewedComponent(&m_timeline, false); // false = don't delete when replaced
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

            m_timeline.onShowTrackInLibraryRequested = [this](TrackId trackId)
            {
                if (m_onShowTrackInLibrary)
                    m_onShowTrackInLibrary(trackId);
            };

            m_timeline.onShowTrackDetailsRequested = [this](TrackId trackId)
            {
                if (m_onShowTrackDetails)
                    m_onShowTrackDetails(trackId);
            };

            m_timeline.onMixPlaybackReloadRequested = [this]()
            {
                // Reload mix in playback controller for hot-reloading gain/envelope changes
                if (m_playbackController && m_node)
                {
                    const bool wasPlaying = m_playbackController->isPlaying();
                    const double playbackPosition = m_playbackController->getCurrentPositionSeconds();
                    auto& mixLoader = m_node->getMixProjectLoader();
                    if (m_playbackController->loadMix(&mixLoader))
                    {
                        if (wasPlaying)
                        {
                            m_playbackController->playMixFrom(playbackPosition);
                        }
                        else
                        {
                            m_playbackController->seek(playbackPosition);
                        }
                    }
                }
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
                
            };

            // Set up seek callback for clicking on timeline
            m_timeline.onSeekRequested = [this](double timePosition)
            {
                if (m_playbackController && m_playbackController->isMixMode())
                {
                    m_playbackController->seek(timePosition);
                    m_timeline.setMixPlaybackPosition(timePosition);
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
            // Reload the read-only state from database if we have a node
            if (m_node)
            {
                const auto& mixManager = database::theTrackLibrary.getMixManager();
                auto mixInfo = mixManager.getMix(m_node->getMixInfo().mixId);
                bool wasReadOnly = m_isReadOnly;
                m_isReadOnly = mixInfo.exportFolder.has_value() && !mixInfo.exportFolder->empty();

                // If read-only state changed, update the timeline
                if (wasReadOnly != m_isReadOnly)
                {
                    spdlog::info("[MixEditor] Read-only state changed from {} to {} for mix {}",
                                wasReadOnly, m_isReadOnly, m_node->getMixInfo().mixId);
                    m_timeline.setReadOnly(m_isReadOnly);
                }
            }

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
                const auto text = "[LOCKED] Mix is Read-Only (Exported) - Right-click in tree to unlock";
                juce::GlyphArrangement glyphs;
                glyphs.addLineOfText(g.getCurrentFont(), text, 0.0f, 0.0f);
                const auto textWidth = static_cast<int>(glyphs.getBoundingBox(0, -1, true).getWidth());
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
            if (m_node == nullptr)
                return false;

            // Ctrl+Z for undo
            if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
            {
                spdlog::info("MixEditorComponent: Undo requested");
                
                const auto mixId = m_node->getMixProjectLoader().getMixId(); //-V595
                
                if (theUndoManager.canUndo(mixId))
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
                    
                    if (theUndoManager.undo(mixId))
                    {
                        // Refresh the mix after undo
                        if (m_node)
                        {
                            spdlog::info("Refreshing mix after undo");
                            m_node->refreshCache(true);  // Force a complete refresh
                            
                            // Reload mix - atomic swap handles thread safety
                            if (m_playbackController)
                            {
                                // Reload the mix in the playback controller
                                auto& mixLoader = m_node->getMixProjectLoader();
                                spdlog::debug("[UNDO] Reloading mix in playback controller");
                                m_playbackController->loadMix(&mixLoader);

                                // Refresh timeline
                                m_timeline.populateFrom(&mixLoader);
                                m_timeline.repaint();
                                m_viewport.repaint();

                                // Resume playback if it was playing
                                if (wasPlaying)
                                {
                                    spdlog::debug("[UNDO] Resuming playback at position {}", playbackPosition);
                                    m_playbackController->playMixFrom(playbackPosition);
                                }
                            }
                            else
                            {
                                // No playback controller, just refresh timeline
                                auto& mixLoader = m_node->getMixProjectLoader();
                                m_timeline.populateFrom(&mixLoader);
                                m_timeline.repaint();
                                m_viewport.repaint();
                            }
                        }
                    }
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
                
                const auto mixId = m_node->getMixProjectLoader().getMixId();                 //-V595
                if (theUndoManager.canRedo(mixId))
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
                    
                    if (theUndoManager.redo(mixId))
                    {
                        // Refresh the mix after redo
                        if (m_node)
                        {
                            spdlog::info("Refreshing mix after redo");
                            m_node->refreshCache(true);  // Force a complete refresh
                            
                            // Reload mix - atomic swap handles thread safety
                            if (m_playbackController)
                            {
                                // Reload the mix in the playback controller
                                auto& mixLoader = m_node->getMixProjectLoader();
                                spdlog::debug("[REDO] Reloading mix in playback controller");
                                m_playbackController->loadMix(&mixLoader);

                                // Refresh timeline
                                m_timeline.populateFrom(&mixLoader);
                                m_timeline.repaint();
                                m_viewport.repaint();

                                // Resume playback if it was playing
                                if (wasPlaying)
                                {
                                    spdlog::debug("[REDO] Resuming playback at position {}", playbackPosition);
                                    m_playbackController->playMixFrom(playbackPosition);
                                }
                            }
                            else
                            {
                                // No playback controller, just refresh timeline
                                auto& mixLoader = m_node->getMixProjectLoader();
                                m_timeline.populateFrom(&mixLoader);
                                m_timeline.repaint();
                                m_viewport.repaint();
                            }
                        }
                    }
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
                m_node->release(REFCOUNT_DEBUG_ARGS);
                m_node = nullptr;
            }
        }

        void MixEditorComponent::loadMix(database::MixNode *node)
        {
            spdlog::info("[MixEditor] loadMix called with node: {}", node ? "valid" : "null");
            
            if (!node)
            {
                spdlog::error("[MixEditor] loadMix called with null node pointer");
                return;
            }

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
                        if (!trackInfo->artist_name.empty() && !trackInfo->title.empty())
                        {
                            req.trackName = std::format("{} - {}", trackInfo->artist_name, trackInfo->title);
                        }
                        else if (!trackInfo->title.empty())
                        {
                            req.trackName = trackInfo->title;
                        }
                        else
                        {
                            req.trackName = req.filePath.filename().string();
                        }
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
                        [this, loaderPtr, task](bool /*success*/) {
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
            m_timeline.viewportResized();
            const auto timelineEnd = std::chrono::high_resolution_clock::now();
            
            const auto viewportDuration = std::chrono::duration_cast<std::chrono::microseconds>(viewportEnd - viewportStart);
            const auto timelineDuration = std::chrono::duration_cast<std::chrono::microseconds>(timelineEnd - timelineStart);
            const auto totalDuration = std::chrono::duration_cast<std::chrono::microseconds>(timelineEnd - startTime);
            
            spdlog::debug("MixEditorComponent::resized - Performance:");
            spdlog::debug("  Viewport.setBounds: {} µs", viewportDuration.count());
            spdlog::debug("  Timeline.viewportResized: {} µs", timelineDuration.count());
            spdlog::debug("  Total: {} µs", totalDuration.count());
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

            spdlog::info("[EDIT-CALLBACK] updateCueAttachInData called - OrderInMix={}, TrackId={}", orderInMix, updatedTrack.trackId);
            spdlog::info("[EDIT-CALLBACK]   Incoming: CueStart={}ms, CueEnd={}ms, AttachFrom={}ms, AttachTo={}ms",
                        updatedTrack.cueStart.count(), updatedTrack.cueEnd.count(),
                        updatedTrack.attachFrom.count(), updatedTrack.attachTo.count());

            // Get access to the mix tracks
            auto& mixTracks = const_cast<audio::MixProjectLoader&>(m_node->getMixProjectLoader()).getMixTracks();

            // Find and update the track by orderInMix AND trackId (to handle duplicate orderInMix)
            for (auto& track : mixTracks)
            {
                // Match both orderInMix AND trackId to handle data corruption cases
                if (track.orderInMix == orderInMix && track.trackId == updatedTrack.trackId)
                {
                    spdlog::info("[EDIT-CALLBACK]   Before update: CueStart={}ms, CueEnd={}ms, AttachFrom={}ms, AttachTo={}ms",
                                track.cueStart.count(), track.cueEnd.count(),
                                track.attachFrom.count(), track.attachTo.count());

                    // Check if cue or attach points actually changed
                    const auto cueStartChanged = track.cueStart != updatedTrack.cueStart;
                    const auto cueEndChanged = track.cueEnd != updatedTrack.cueEnd;
                    const auto attachChanged = track.attachFrom != updatedTrack.attachFrom ||
                                              track.attachTo != updatedTrack.attachTo;

                    spdlog::info("[EDIT-CALLBACK] attachChanged={}, linkEnvelopePointsToAttachPoints={}",
                               attachChanged, config::theSettings.mixEditingSettings.linkEnvelopePointsToAttachPoints.get());

                    // Scale envelope points if attach points changed and feature is enabled
                    if (attachChanged && config::theSettings.mixEditingSettings.linkEnvelopePointsToAttachPoints.get())
                    {
                        // Get track duration from TrackInfo
                        const auto trackInfo = database::theTrackLibrary.getTrackById(track.trackId);
                        if (trackInfo.has_value())
                        {
                            spdlog::info("[EDIT-CALLBACK] Scaling envelope points: attachFrom {}ms->{}ms, attachTo {}ms->{}ms",
                                       track.attachFrom.count(), updatedTrack.attachFrom.count(),
                                       track.attachTo.count(), updatedTrack.attachTo.count());

                            track.scaleEnvelopePointsForAttachChange(
                                track.attachFrom,
                                updatedTrack.attachFrom,
                                track.attachTo,
                                updatedTrack.attachTo,
                                trackInfo.value().duration);

                            spdlog::info("[EDIT-CALLBACK] Envelope points scaled, new count: {}", track.envelopePoints.size());
                        }
                        else
                        {
                            spdlog::warn("[EDIT-CALLBACK] Could not get TrackInfo for trackId {}, envelope points not scaled", track.trackId);
                        }
                    }

                    track.cueStart = updatedTrack.cueStart;
                    track.cueEnd = updatedTrack.cueEnd;
                    track.attachFrom = updatedTrack.attachFrom;
                    track.attachTo = updatedTrack.attachTo;

                    spdlog::info("[EDIT-CALLBACK]   After update: CueStart={}ms, CueEnd={}ms, AttachFrom={}ms, AttachTo={}ms",
                                track.cueStart.count(), track.cueEnd.count(),
                                track.attachFrom.count(), track.attachTo.count());
                    spdlog::info("[EDIT-CALLBACK]   Changes: CueStart={}, CueEnd={}, Attach={}",
                                cueStartChanged, cueEndChanged, attachChanged);
                    
                    // Save changes
                    saveMixChanges();

                    // Verify the change was preserved in memory after save
                    spdlog::info("[EDIT-CALLBACK]   After saveMixChanges: CueStart={}ms, CueEnd={}ms, AttachFrom={}ms, AttachTo={}ms",
                                track.cueStart.count(), track.cueEnd.count(),
                                track.attachFrom.count(), track.attachTo.count());

                    // Tell timeline to reposition this specific track
                    m_timeline.repositionTrack(track.trackId);

                    // Tell playback controller to reload if positions changed
                    // Note: cueStart of first track affects global offset, attach points affect all positions
                    const auto isFirstTrack = (track.orderInMix == 0);
                    const auto needsRecalc = attachChanged || (isFirstTrack && cueStartChanged);

                    if (needsRecalc && m_playbackController)
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

            // Check if mix is read-only
            if (m_isReadOnly)
            {
                showMoveBackDialog();
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

            // Reload mix to update playback state
            if (m_playbackController)
            {
                auto& mixLoader = m_node->getMixProjectLoader();
                m_playbackController->loadMix(&mixLoader);
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
            
            // Update envelope points in MixProjectLoader
            auto& mixTracks = const_cast<audio::MixProjectLoader&>(m_node->getMixProjectLoader()).getMixTracks();

            // Find and update the track by orderInMix
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

            // Reload mix to update playback state (atomic swap handles thread safety)
            if (m_playbackController)
            {
                auto& mixLoader = m_node->getMixProjectLoader();
                m_playbackController->loadMix(&mixLoader);
            }
            else
            {
                spdlog::error("MixEditorComponent::updateEnvelopeInData - No playback controller available");
            }
        }
        
        void MixEditorComponent::updateGainAdjustmentInData(int orderInMix, float newGain, bool saveToDatabase)
        {
            spdlog::warn("=== GAIN CHANGE === updateGainAdjustmentInData called: orderInMix={}, newGain={}, saveToDatabase={}",
                        orderInMix, newGain, saveToDatabase);

            if (!m_node)
            {
                spdlog::error("MixEditorComponent::updateGainAdjustmentInData - No mix node loaded");
                return;
            }

            spdlog::info("Updating gain adjustment for track at position {} to {} (save: {})",
                        orderInMix, newGain, saveToDatabase);

            // Update gain in MixProjectLoader
            auto& mixTracks = const_cast<audio::MixProjectLoader&>(m_node->getMixProjectLoader()).getMixTracks();

            spdlog::warn("=== GAIN CHANGE === MixProjectLoader has {} tracks", mixTracks.size());

            // Find and update the track by orderInMix
            bool trackFound = false;
            for (auto& track : mixTracks)
            {
                if (track.orderInMix == orderInMix)
                {
                    spdlog::warn("=== GAIN CHANGE === Found track {} at position {}, OLD gain={}, NEW gain={}",
                               track.trackId, orderInMix, track.gainAdjustment, newGain);
                    track.gainAdjustment = newGain;
                    trackFound = true;
                    spdlog::info("Updated gain adjustment for track {} at position {} to {}",
                               track.trackId, orderInMix, newGain);

                    // Only save to database if requested (i.e., when OK is clicked)
                    if (saveToDatabase)
                    {
                        saveMixChanges();
                    }

                    break;
                }
            }

            if (!trackFound)
            {
                spdlog::error("=== GAIN CHANGE === Track at position {} NOT FOUND!", orderInMix);
            }

            // Reload mix to update playback state (atomic swap handles thread safety)
            if (m_playbackController)
            {
                spdlog::warn("=== GAIN CHANGE === Calling playbackController->loadMix(), controller state: playing={}, mixMode={}",
                           m_playbackController->isPlaying(), m_playbackController->isMixMode());
                auto& mixLoader = m_node->getMixProjectLoader();
                bool loadResult = m_playbackController->loadMix(&mixLoader);
                spdlog::warn("=== GAIN CHANGE === playbackController->loadMix() returned {}", loadResult);
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
            spdlog::info("[SAVE-DB] saveMixChanges called");

            // Get the current mix info and tracks
            auto &mixProjectLoader = m_node->getMixProjectLoader();
            auto &mixTracks = mixProjectLoader.getMixTracks();

            // Log all track values before saving
            spdlog::info("[SAVE-DB] Saving {} tracks to database", mixTracks.size());
            for (const auto& track : mixTracks)
            {
                spdlog::info("[SAVE-DB]   Track {} (OrderInMix={}): CueStart={}ms, CueEnd={}ms, AttachFrom={}ms, AttachTo={}ms",
                            track.trackId, track.orderInMix,
                            track.cueStart.count(), track.cueEnd.count(),
                            track.attachFrom.count(), track.attachTo.count());
            }

            // Get mix info from database
            auto &mixInfo = mixProjectLoader.getMixInfo();
            mixInfo.totalDuration = mixProjectLoader.calculateMixDuration();

            // Save changes back to database
            spdlog::info("[SAVE-DB] Calling createOrUpdateMix for mix {}", mixInfo.mixId);
            if (database::theTrackLibrary.getMixManager().createOrUpdateMix(mixInfo, mixTracks))
            {
                spdlog::info("[SAVE-DB] Successfully saved mix changes to database");
            }
            else
            {
                spdlog::error("[SAVE-DB] FAILED to save mix changes to database");
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

            // Only load the mix if we're not already in mix mode
            // If we're already in mix mode, the mix is already loaded with the current state
            // and we should NOT reload it (which could fetch stale data from the database)
            if (!m_playbackController->isMixMode())
            {
                spdlog::info("[MixEditor] Loading mix into playback controller (not in mix mode)");

                // Log the current state before loading
                auto& mixLoader = m_node->getMixProjectLoader();
                auto& mixTracks = mixLoader.getMixTracks();
                spdlog::info("[MixEditor] Current mix loader has {} tracks before loading into playback", mixTracks.size());
                if (mixTracks.size() > 1)
                {
                    spdlog::info("[MixEditor]   Track 0: AttachFrom={}ms, AttachTo={}ms",
                                mixTracks[0].attachFrom.count(), mixTracks[0].attachTo.count());
                    spdlog::info("[MixEditor]   Track 1: AttachFrom={}ms, AttachTo={}ms",
                                mixTracks[1].attachFrom.count(), mixTracks[1].attachTo.count());
                }

                if (!m_playbackController->loadMix(&mixLoader))
                {
                    spdlog::error("[MixEditor] Failed to load mix");
                    return;
                }
            }
            else
            {
                spdlog::info("[MixEditor] Already in mix mode, skipping reload to preserve in-memory changes");
            }

            // Play from the specified position
            spdlog::info("[MixEditor] Starting playback at {:.2f}s", startTime);
            m_playbackController->playMixFrom(startTime);
        }
        
        void MixEditorComponent::handleDeleteSelectedTrack()
        {
            spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Entry");
            
            int trackIdToRemove = -1;
            int orderInMixToRemove = -1;

            auto* selectedTrackComponent = m_timeline.getSelectedTrack();
            if (!selectedTrackComponent)
            {
                spdlog::warn("handleDeleteSelectedTrack called but no track selected.");
                spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> No track selected, exiting.");
                return;
            }
            trackIdToRemove = selectedTrackComponent->getTrackId();
            orderInMixToRemove = selectedTrackComponent->getOrderInMix();
            
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
            if (!database::theTrackLibrary.getMixManager().removeTrackFromMixAtOrder(mixId, orderInMixToRemove))
            {
                spdlog::error("Failed to remove track {} at order {} from mix {}", trackIdToRemove, orderInMixToRemove, mixId);
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
            
            // 5. Reload the updated mix data from the database so new adjacencies/attach
            // points match the persisted mix state after deletion.
            spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Reloading mix loader from database.");
            if (!mixLoader.reloadFromDatabase())
            {
                spdlog::critical("CRITICAL: Failed to reload mix loader after deletion! The application state is now inconsistent.");
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Critical Error", "Failed to reload mix data after deletion. Please restart the application.");
                return;
            }

            playbackPosition = std::min(
                playbackPosition,
                std::chrono::duration<double>(mixLoader.calculateMixDuration()).count());
            
            // Update the node's cached summary metadata after successful deletion
            m_node->updateSummaryMetadata(
                static_cast<int>(mixLoader.getMixTracks().size()),
                mixLoader.calculateMixDuration()
            );
            if (m_onMixSummaryChanged)
                m_onMixSummaryChanged();

            // 6. Refresh the timeline UI first (but only if not playing - otherwise do it after)
            if (!wasPlaying)
            {
                spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Refreshing timeline UI.");
                m_timeline.refreshAfterDeletion(orderInMixToRemove);
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
                m_timeline.refreshAfterDeletion(orderInMixToRemove);
                
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
                
                // Update the node's cached summary metadata after successful paste
                m_node->updateSummaryMetadata(
                    static_cast<int>(mixLoader.getMixTracks().size()),
                    mixLoader.calculateMixDuration()
                );
                if (m_onMixSummaryChanged)
                    m_onMixSummaryChanged();

                // Refresh the timeline
                spdlog::info("[PASTE_DB] Refreshing timeline with updated mix");
                m_timeline.populateFrom(&mixLoader);

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
                else
                {
                    m_timeline.populateFrom(&mixLoader);
                }

                // Update the node's cached summary metadata (track count and total duration)
                // This ensures the metadata dialog shows the correct values without affecting
                // the active track editing session or triggering broader cache invalidation
                m_node->updateSummaryMetadata(
                    static_cast<int>(mixLoader.getMixTracks().size()),
                    mixLoader.calculateMixDuration()
                );
                if (m_onMixSummaryChanged)
                    m_onMixSummaryChanged();

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
                        m_playheadOverlay.setPlayheadPosition(positionSeconds, pixelsPerSecond);
                        m_markerRuler.setPlaybackPosition(positionSeconds * 1000.0);
                    }
                    // If playhead is not visible, skip the expensive repaint operations
                    
                    // Check if we've reached the end
                    const double totalSeconds = m_playbackController->getLengthInSeconds();
                    if (totalSeconds > 0 && positionSeconds >= totalSeconds)
                    {
                        spdlog::info("Playback reached end of mix");
                        m_playbackController->stop();
                        m_playheadOverlay.setPlayheadPosition(-1.0, pixelsPerSecond);
                    }
                }
                else
                {
                    // Not playing - hide playhead
                    m_playheadOverlay.setPlayheadPosition(-1.0, m_timeline.getPixelsPerSecond());
                }
            }
            else
            {
                // Not in mix mode - hide playhead
                m_playheadOverlay.setPlayheadPosition(-1.0, m_timeline.getPixelsPerSecond());
            }
        }
        
        void MixEditorComponent::loadMixMarkers()
        {
            if (!m_node)
                return;
                
            const auto mixId = m_node->getMixProjectLoader().getMixId();
            const auto& mixMarkerManager = database::theTrackLibrary.getTrackDatabase().getMixMarkerManager();
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
            auto& mixMarkerManager = database::theTrackLibrary.getTrackDatabase().getMixMarkerManager();
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
            const auto& mixMarkerManager = database::theTrackLibrary.getTrackDatabase().getMixMarkerManager();
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
                        const auto& mixMarkerManager = database::theTrackLibrary.getTrackDatabase().getMixMarkerManager();
                        auto marker = mixMarkerManager.getMarker(markerId);
                        if (marker.has_value())
                        {
                            marker->comment = dialog->getTextEditorContents("comment").toStdString();
                            saveMixMarker(marker.value());
                        }
                    }
                    else if (result == 2) // Delete
                    {
                        auto& mixMarkerManager = database::theTrackLibrary.getTrackDatabase().getMixMarkerManager();
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
                            assert(marker.marker_id == 0); // New marker
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

                    // Notify parent component to refresh navigation tree
                    if (m_onMixExportStatusChanged)
                    {
                        m_onMixExportStatusChanged();
                    }

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
                    
                    requests.emplace_back(std::make_pair(static_cast<int>(trackInfo->trackId), needsLoading));
                }
            }
            
            return requests;
        }
        
        void MixEditorComponent::populateTimeline(audio::MixProjectLoader* loader)
        {
            if (!loader)
                return;

            // Check if we're loading a different mix - if so, reset viewport to start
            const auto newMixId = loader->getMixId();
            const bool isDifferentMix = (m_currentMixId != newMixId);

            if (isDifferentMix)
            {
                spdlog::info("[MixEditor] Switching from mix {} to mix {}, resetting viewport to start",
                           m_currentMixId, newMixId);
                m_currentMixId = newMixId;
                // Reset viewport scroll position to the beginning
                m_viewport.setViewPosition(0, 0);
            }

            // Load the mix into the timeline
            m_timeline.populateFrom(loader);

            // Calculate mix duration and set it on the ruler
            const auto mixDuration = loader->calculateMixDuration();
            m_markerRuler.setMixDuration(mixDuration);

            // Load markers for this mix
            loadMixMarkers();

            // Ensure timeline has keyboard focus for playback controls
            m_timeline.grabKeyboardFocus();
        }
        
    } // namespace ui
} // namespace jucyaudio
