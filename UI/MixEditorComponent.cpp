#include <Database/Includes/INavigationNode.h>
#include <UI/MainComponent.h>
#include <UI/MixEditorComponent.h>
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

            // Set the timeline as the component to be viewed by the viewport.
            m_viewport.setViewedComponent(&m_timeline, false); // false = don't delete when replaced
            m_viewport.setScrollBarsShown(true, true);
            addAndMakeVisible(m_viewport);
                        
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
                handleMixPlayback(startTime);
            };
            
            // Double-click always plays (doesn't toggle)
            m_timeline.onMixPlaybackAlwaysRequested = [this](double startTime)
            {
                handleMixPlayback(startTime, true);
            };
            
            // Initialize audio
            m_mixPlaybackEngine = std::make_unique<audio::MixPlaybackEngine>();
            m_audioDeviceManager = std::make_unique<juce::AudioDeviceManager>();
            
            // Set up audio device - but don't add callback yet (will be added when playback starts)
            
            // Set up playback timer
            m_playbackTimer.owner = this;
        }
        
        MixEditorComponent::~MixEditorComponent()
        {
            // Stop playback if playing
            if (m_isPlaying)
            {
                stopMixPlayback();
            }
            
            // Clean up audio
            if (m_audioDeviceManager && m_mixPlaybackEngine)
            {
                m_audioDeviceManager->removeAudioCallback(m_mixPlaybackEngine.get());
            }
            
            // Unload mix before cleanup
            unloadMix();
        }

        void MixEditorComponent::forceRefresh()
        {
            m_timeline.repaint();
            m_viewport.repaint();
            resized(); // Recalculate viewport content
        }

        void MixEditorComponent::setPlaybackCallback(std::function<void(const juce::File &, double)> callback)
        {
            m_timeline.onPlaybackRequested = callback;
        }

        void MixEditorComponent::setSeekCallback(std::function<void(double)> callback)
        {
            m_timeline.onSeekRequested = callback;
        }
        
        void MixEditorComponent::setMixPlaybackCallback(std::function<void(double)> callback)
        {
            m_timeline.onMixPlaybackRequested = callback;
        }
        
        void MixEditorComponent::setOnMixPlaybackStarting(std::function<void()> callback)
        {
            m_onMixPlaybackStarting = callback;
        }
        
        void MixEditorComponent::setOnMixPlaybackStopped(std::function<void()> callback)
        {
            m_onMixPlaybackStopped = callback;
        }

        void MixEditorComponent::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ListBox::backgroundColourId).darker());
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
                    undoManager.undo(mixId, [this]() {
                        // Refresh the mix after undo
                        if (m_node)
                        {
                            spdlog::info("Refreshing mix after undo");
                            m_node->refreshCache(true);  // Force a complete refresh
                            m_timeline.populateFrom(&(m_node->getMixProjectLoader()));
                            m_timeline.repaint();
                            m_viewport.repaint();
                        }
                    });
                }
                else
                {
                    spdlog::info("No undo available for mix {}", mixId);
                }
                return true;
            }
            // Ctrl+Y for redo
            else if (key == juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0))
            {
                spdlog::info("MixEditorComponent: Redo requested");
                
                auto& undoManager = database::theTrackLibrary.getUndoManager();
                const auto mixId = m_node->getMixProjectLoader().getMixId();
                
                if (undoManager.canRedo(mixId))
                {
                    undoManager.redo(mixId, [this]() {
                        // Refresh the mix after redo
                        if (m_node)
                        {
                            spdlog::info("Refreshing mix after redo");
                            m_node->refreshCache(true);  // Force a complete refresh
                            m_timeline.populateFrom(&(m_node->getMixProjectLoader()));
                            m_timeline.repaint();
                            m_viewport.repaint();
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
                m_node->release(REFCOUNT_DEBUG_ARGS);
                m_node = nullptr;
            }
        }

        void MixEditorComponent::loadMix(database::MixNode *node)
        {
            spdlog::info("[MixEditor] loadMix called with node: {}", node ? "valid" : "null");
            assert(node != nullptr && "MixNode should not be null in loadMix()");
            if (m_node)
            {
                spdlog::info("[MixEditor] Releasing previous node");
                m_node->release(REFCOUNT_DEBUG_ARGS); // Release previous node if any
            }
            m_node = node; // Take ownership of the new node
            node->retain(REFCOUNT_DEBUG_ARGS); // Retain the node to ensure it stays valid
            node->refreshCache(false);
            
            auto& loader = node->getMixProjectLoader();
            spdlog::info("[MixEditor] Loading mix with {} tracks into timeline", loader.getMixTracks().size());
            
            // Load the mix into the playback engine
            if (m_mixPlaybackEngine)
            {
                bool loadSuccess = m_mixPlaybackEngine->loadMix(&loader);
                spdlog::info("[MixEditor] Mix loaded into playback engine: {}", loadSuccess ? "success" : "failed");
            }
            else
            {
                spdlog::error("[MixEditor] m_mixPlaybackEngine is null!");
            }
            
            m_timeline.populateFrom(&loader);
            
            // Ensure timeline has keyboard focus for playback controls
            m_timeline.grabKeyboardFocus();
        }

        void MixEditorComponent::resized()
        {
            // The viewport now fills the entire editor area.
            m_viewport.setBounds(getLocalBounds());
            
            // Notify the timeline that the viewport has resized
            m_timeline.viewportResized();
        }

        void MixEditorComponent::updateCueAttachInData(int orderInMix, const database::MixTrack& updatedTrack)
        {
            if (!m_node)
            {
                spdlog::error("MixEditorComponent::updateCueAttachInData - No mix node loaded");
                return;
            }
            
            spdlog::info("Updating cue/attach points for track at position {}", orderInMix);
            
            // Get access to the mix tracks
            auto& mixTracks = const_cast<audio::MixProjectLoader&>(m_node->getMixProjectLoader()).getMixTracks();
            
            // Find and update the track by orderInMix
            for (auto& track : mixTracks)
            {
                if (track.orderInMix == orderInMix)
                {
                    // Check if cue or attach points actually changed
                    const auto cueStartChanged = track.cueStart != updatedTrack.cueStart;
                    const auto attachChanged = track.attachFrom != updatedTrack.attachFrom || 
                                              track.attachTo != updatedTrack.attachTo;
                    
                    track.cueStart = updatedTrack.cueStart;
                    track.cueEnd = updatedTrack.cueEnd;
                    track.attachFrom = updatedTrack.attachFrom;
                    track.attachTo = updatedTrack.attachTo;
                    spdlog::info("Updated cue/attach points for track at position {}", orderInMix);
                    
                    // Save changes
                    saveMixChanges();
                    
                    // Tell timeline to reposition this specific track
                    m_timeline.repositionTrack(track.trackId);
                    
                    // Tell playback engine to recalculate track positions if needed
                    // Note: cueStart of first track affects global offset, attach points affect all positions
                    const auto isFirstTrack = (track.orderInMix == 0);
                    const auto needsRecalc = attachChanged || (isFirstTrack && cueStartChanged);
                    
                    if (needsRecalc && m_mixPlaybackEngine && m_mixPlaybackEngine->isMixLoaded() && 
                        m_mixPlaybackEngine->getMixLoader() == &m_node->getMixProjectLoader())
                    {
                        spdlog::info("Recalculating playback engine track positions after {} change",
                                    attachChanged ? "attach point" : "first track cueStart");
                        m_mixPlaybackEngine->recalculateTrackPositions();
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
            
            spdlog::info("Updating envelope for track at position {} with {} points", orderInMix, points.size());
            
            // Get access to the mix tracks
            auto& mixTracks = const_cast<audio::MixProjectLoader&>(m_node->getMixProjectLoader()).getMixTracks();
            
            // Find and update the track by orderInMix
            for (auto& track : mixTracks)
            {
                if (track.orderInMix == orderInMix)
                {
                    track.envelopePoints = points;
                    spdlog::info("Updated envelope points for track at position {}", orderInMix);
                    
                    // Save changes
                    saveMixChanges();
                    break;
                }
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
            auto mixId = mixProjectLoader.getMixId();
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
            spdlog::info("[MixEditor] handleMixPlayback called - startTime: {}, alwaysPlay: {}, m_isPlaying: {}", 
                        startTime, alwaysPlay, m_isPlaying);
            
            // Special case: negative value means stop
            if (startTime < 0)
            {
                if (m_isPlaying)
                {
                    spdlog::info("[MixEditor] Stopping playback (negative startTime)");
                    stopMixPlayback();
                }
                return;
            }
            
            spdlog::info("[MixEditor] {} playback at {:.2f}s", 
                        alwaysPlay ? "Starting" : "Toggling", startTime);
            
            if (!m_node)
            {
                spdlog::error("[MixEditor] No mix node loaded");
                return;
            }
            
            if (!m_mixPlaybackEngine)
            {
                spdlog::error("[MixEditor] m_mixPlaybackEngine is null!");
                return;
            }
            
            if (m_isPlaying && !alwaysPlay)
            {
                // Toggle off (only for space key)
                spdlog::info("[MixEditor] Toggling playback off");
                stopMixPlayback();
            }
            else
            {
                // Load the mix into the playback engine if not already loaded
                bool mixLoaded = m_mixPlaybackEngine->isMixLoaded();
                bool correctLoader = m_mixPlaybackEngine->getMixLoader() == &m_node->getMixProjectLoader();
                spdlog::info("[MixEditor] Mix loaded: {}, Correct loader: {}", mixLoaded, correctLoader);
                
                if (!mixLoaded || !correctLoader)
                {
                    spdlog::info("[MixEditor] Loading mix into playback engine");
                    if (!m_mixPlaybackEngine->loadMix(&m_node->getMixProjectLoader()))
                    {
                        spdlog::error("[MixEditor] Failed to load mix into playback engine");
                        return;
                    }
                    spdlog::info("[MixEditor] Mix successfully loaded into playback engine");
                }
                
                // Set position and start playback
                auto positionMs = std::chrono::milliseconds(static_cast<int64_t>(startTime * 1000));
                spdlog::info("[MixEditor] Setting playback position to {} ms", positionMs.count());
                m_mixPlaybackEngine->setPosition(positionMs);
                startMixPlayback();
            }
        }
        
        void MixEditorComponent::startMixPlayback()
        {
            spdlog::info("[MixEditor] startMixPlayback called, m_isPlaying={}", m_isPlaying);
            
            if (!m_isPlaying)
            {
                // Ensure audio device is open
                if (m_audioDeviceManager)
                {
                    auto* currentDevice = m_audioDeviceManager->getCurrentAudioDevice();
                    spdlog::info("[MixEditor] Current audio device: {}", currentDevice ? currentDevice->getName().toStdString() : "none");
                    
                    if (!currentDevice)
                    {
                        spdlog::info("[MixEditor] Initializing audio device with default settings");
                        auto result = m_audioDeviceManager->initialiseWithDefaultDevices(0, 2);
                        if (result.isNotEmpty())
                        {
                            spdlog::error("[MixEditor] Failed to initialize audio device: {}", result.toStdString());
                            return;
                        }
                        
                        currentDevice = m_audioDeviceManager->getCurrentAudioDevice();
                        if (currentDevice)
                        {
                            spdlog::info("[MixEditor] Audio device initialized: {}", currentDevice->getName().toStdString());
                            spdlog::info("[MixEditor] Sample rate: {}, Buffer size: {}", 
                                currentDevice->getCurrentSampleRate(), 
                                currentDevice->getCurrentBufferSizeSamples());
                        }
                        
                        spdlog::info("[MixEditor] Adding audio callback to device");
                        m_audioDeviceManager->addAudioCallback(m_mixPlaybackEngine.get());
                        
                        // Ensure the engine is prepared after adding the callback
                        if (currentDevice)
                        {
                            m_mixPlaybackEngine->prepareToPlay(currentDevice->getCurrentBufferSizeSamples(), 
                                                               currentDevice->getCurrentSampleRate());
                            spdlog::info("[MixEditor] Prepared playback engine after adding callback");
                        }
                    }
                    else if (currentDevice)
                    {
                        // Device exists but we should ensure the engine is prepared
                        m_mixPlaybackEngine->prepareToPlay(currentDevice->getCurrentBufferSizeSamples(), 
                                                           currentDevice->getCurrentSampleRate());
                        spdlog::info("[MixEditor] Ensured playback engine is prepared");
                    }
                }
                else
                {
                    spdlog::error("[MixEditor] m_audioDeviceManager is null!");
                    return;
                }
                
                m_isPlaying = true;
                spdlog::info("[MixEditor] Set m_isPlaying to true");
                
                // Stop any single track playback and update UI state
                if (m_onMixPlaybackStarting)
                {
                    spdlog::info("[MixEditor] Calling m_onMixPlaybackStarting callback");
                    m_onMixPlaybackStarting();
                    spdlog::info("[MixEditor] Finished m_onMixPlaybackStarting callback");
                }
                else
                {
                    spdlog::warn("[MixEditor] m_onMixPlaybackStarting callback is not set!");
                }
                
                // Unpause the playback engine
                if (m_mixPlaybackEngine)
                {
                    spdlog::info("[MixEditor] Unpausing playback engine");
                    m_mixPlaybackEngine->setPaused(false);
                    
                    auto position = m_mixPlaybackEngine->getPosition();
                    spdlog::info("[MixEditor] Current playback position: {} ms", position.count());
                }
                
                // Start the timer to update playback position
                spdlog::info("[MixEditor] Starting playback timer");
                m_playbackTimer.startTimer(50); // Update at 20Hz
                
                // Update timeline to show we're playing
                auto positionSeconds = m_mixPlaybackEngine->getPosition().count() / 1000.0;
                spdlog::info("[MixEditor] Setting timeline playback position to {} seconds", positionSeconds);
                m_timeline.setMixPlaybackPosition(positionSeconds);
            }
            else
            {
                spdlog::info("[MixEditor] Already playing, ignoring start request");
            }
        }
        
        void MixEditorComponent::stopMixPlayback()
        {
            spdlog::info("MixEditorComponent::stopMixPlayback");
            
            if (m_isPlaying)
            {
                m_isPlaying = false;
                
                // Stop the timer
                m_playbackTimer.stopTimer();
                
                // Pause the playback engine instead of closing the device
                if (m_mixPlaybackEngine)
                {
                    m_mixPlaybackEngine->setPaused(true);
                }
                
                // Reset playback position display
                m_timeline.setMixPlaybackPosition(-1.0); // Hide the red playhead
                
                // Notify that mix has stopped
                if (m_onMixPlaybackStopped)
                {
                    m_onMixPlaybackStopped();
                }
            }
        }
        
        void MixEditorComponent::handleDeleteSelectedTrack()
        {
            // 1. Pre-condition checks
            auto* selectedTrackComponent = m_timeline.getSelectedTrack();
            if (!selectedTrackComponent)
            {
                spdlog::warn("handleDeleteSelectedTrack called but no track selected.");
                return;
            }
            if (!m_node)
            {
                spdlog::error("handleDeleteSelectedTrack called but no mix node loaded.");
                return;
            }
            
            auto& mixLoader = m_node->getMixProjectLoader();
            const auto trackIdToRemove = selectedTrackComponent->getTrackId();
            const auto mixId = mixLoader.getMixId();
            
            // 2. Confirmation Dialog & Logic
            // This logic is ported from the old TimelineComponent::deleteSelectedTrack
            bool shouldRemoveFromWorkingSet = false;
            
            if (config::theSettings.mixEditingSettings.removeFromWorkingSetOnDelete.get())
            {
                const auto& mixTracks = mixLoader.getMixTracks();
                int trackOccurrences = 0;
                for (const auto& mixTrack : mixTracks)
                {
                    if (mixTrack.trackId == trackIdToRemove)
                    {
                        trackOccurrences++;
                    }
                }
                
                if (trackOccurrences == 1)
                {
                    const auto& mixInfo = mixLoader.getMixInfo();
                    if (mixInfo.source_ws_id > 0)
                    {
                        if (config::theSettings.mixEditingSettings.askBeforeRemovingFromWorkingSet.get())
                        {
                            const auto result = juce::AlertWindow::showYesNoCancelBox(
                                juce::AlertWindow::QuestionIcon,
                                "Remove Track",
                                "Remove this track from the mix?\n\nAlso remove from the source working set?",
                                "Remove from Both",
                                "Remove from Mix Only",
                                "Cancel");
                            
                            if (result == 0) // Cancel
                            {
                                spdlog::info("User cancelled track removal.");
                                return;
                            }
                            else if (result == 1) // Yes - Remove from both
                            {
                                shouldRemoveFromWorkingSet = true;
                            }
                        }
                        else
                        {
                            shouldRemoveFromWorkingSet = true;
                        }
                    }
                }
            }
            else
            {
                // If not removing from working set, just show a simple confirmation
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
            
            // 3. Stop playback properly using the standard stop method
            const bool wasPlaying = m_isPlaying;
            if (wasPlaying)
            {
                spdlog::info("Stopping playback properly before deletion.");
                // Use the proper stop method which resets all state correctly
                stopMixPlayback();
            }
            
            // 4. Ensure audio is fully stopped and unload mix from engine
            if (m_audioDeviceManager && m_mixPlaybackEngine)
            {
                // Make absolutely sure the callback is removed
                m_audioDeviceManager->removeAudioCallback(m_mixPlaybackEngine.get());
                
                // Give the audio thread a moment to fully stop
                juce::Thread::sleep(50);
                
                // Unload the mix from the engine BEFORE we modify the data
                // This ensures no dangling pointers to track data
                m_mixPlaybackEngine->unloadMix();
                spdlog::info("Unloaded mix from playback engine before deletion.");
            }
            
            // 5. Perform DB Deletions
            if (!database::theTrackLibrary.getMixManager().removeTrackFromMix(mixId, trackIdToRemove))
            {
                spdlog::error("Failed to remove track {} from mix {}", trackIdToRemove, mixId);
                if (wasPlaying)
                {
                    m_audioDeviceManager->addAudioCallback(m_mixPlaybackEngine.get());
                }
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", "Failed to remove track from database.");
                return;
            }
            spdlog::info("Track {} removed from mix in database.", trackIdToRemove);
            
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
            
            // 6. Reload data model from DB
            if (!mixLoader.reloadFromDatabase())
            {
                spdlog::critical("CRITICAL: Failed to reload mix loader from DB after deletion! The application state is now inconsistent.");
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Critical Error", "Failed to reload mix data after deletion. Please restart the application.");
                return;
            }
            spdlog::info("Mix loader reloaded from database.");
            
            // 7. Re-initialize playback engine with the new, reloaded data
            m_mixPlaybackEngine->loadMix(&mixLoader);
            spdlog::info("Playback engine re-loaded with new mix data.");
            
            // 8. Refresh the entire timeline UI
            m_timeline.refreshAfterDeletion(trackIdToRemove);
            spdlog::info("Timeline UI refreshed efficiently.");
            
            // 9. DON'T re-attach the audio callback here - let the user start playback manually
            // This ensures clean state and proper initialization when they press play again
            spdlog::info("Deletion process complete. Ready for manual playback restart.");
        }
        
        void MixEditorComponent::updatePlaybackPosition()
        {
            if (m_isPlaying && m_mixPlaybackEngine)
            {
                // Get current position from engine
                const auto positionMs = m_mixPlaybackEngine->getPosition();
                const double positionSeconds = positionMs.count() / 1000.0;
                
                // Update timeline display
                m_timeline.setMixPlaybackPosition(positionSeconds);
                
                // Check if we've reached the end
                if (positionMs >= m_mixPlaybackEngine->getTotalDuration())
                {
                    spdlog::info("Playback reached end of mix");
                    stopMixPlayback();
                }
            }
        }
    } // namespace ui
} // namespace jucyaudio
