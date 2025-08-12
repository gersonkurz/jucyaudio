#include <Database/Includes/INavigationNode.h>
#include <UI/MainComponent.h>
#include <UI/MixEditorComponent.h>
#include <UI/PlaybackController.h>
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
                handleMixPlayback(startTime, false);
            };
            
            // Set up double-click callback (always play)
            m_timeline.onMixPlaybackAlwaysRequested = [this](double startTime)
            {
                handleMixPlayback(startTime, true);
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
            stopTimer();
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
            
            // Load the mix into the playback controller
            if (m_playbackController)
            {
                bool loadSuccess = m_playbackController->loadMix(&loader);
                spdlog::info("[MixEditor] Mix loaded into playback controller: {}", loadSuccess ? "success" : "failed");
            }
            else
            {
                spdlog::error("[MixEditor] m_playbackController is null!");
            }
            
            m_timeline.populateFrom(&loader);
            
            // Start timer to monitor playback state
            startTimer(50); // 20Hz update rate
            
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
        
        void MixEditorComponent::updateEnvelopeInData(int orderInMix, const std::vector<database::EnvelopePoint>& points)
        {
            if (!m_node)
            {
                spdlog::error("MixEditorComponent::updateEnvelopeInData - No mix node loaded");
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
            spdlog::info("[MixEditor] handleMixPlayback called - startTime: {}", startTime);
            
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
            // 1. Pre-condition checks
            auto* selectedTrackComponent = m_timeline.getSelectedTrack();
            if (!selectedTrackComponent)
            {
                spdlog::warn("handleDeleteSelectedTrack called but no track selected.");
                spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> No track selected, exiting.");
                return;
            }
            if (!m_node)
            {
                spdlog::error("handleDeleteSelectedTrack called but no mix node loaded.");
                spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> No mix node loaded, exiting.");
                return;
            }
            
            auto& mixLoader = m_node->getMixProjectLoader();
            const auto trackIdToRemove = selectedTrackComponent->getTrackId();
            const auto mixId = mixLoader.getMixId();
            spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Attempting to delete track {} from mix {}", trackIdToRemove, mixId);

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
                                spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> User cancelled.");
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
                    spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> User cancelled.");
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
                
                // Give the audio thread a moment to fully stop
                juce::Thread::sleep(50);
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
            
            // 6. Re-load mix in playback controller with the new data
            if (m_playbackController)
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
                
                // 7. Resume playback if we were playing before deletion
                if (wasPlaying)
                {
                    // Small delay to ensure everything is set up
                    juce::Thread::sleep(50);
                    
                    spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Resuming playback from position {}", playbackPosition);
                    m_playbackController->playMixFrom(playbackPosition);
                    
                    // Force ensure it's really playing
                    spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Final state check: isPlaying={}, isMixMode={}", 
                        m_playbackController->isPlaying(),
                        m_playbackController->isMixMode());
                }
            }
            
            // 8. Refresh the entire timeline UI
            spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Refreshing timeline UI.");
            m_timeline.refreshAfterDeletion(trackIdToRemove);
            
            // 9. Ready for playback
            spdlog::debug("JUCYAUDIO: handleDeleteSelectedTrack -> Exit");
        }
        
        void MixEditorComponent::timerCallback()
        {
            if (m_playbackController && m_playbackController->isMixMode())
            {
                if (m_playbackController->isPlaying())
                {
                    // Update timeline playback position
                    const double positionSeconds = m_playbackController->getCurrentPositionSeconds();
                    m_timeline.setMixPlaybackPosition(positionSeconds);
                    
                    // Check if we've reached the end
                    const double totalSeconds = m_playbackController->getLengthInSeconds();
                    if (totalSeconds > 0 && positionSeconds >= totalSeconds)
                    {
                        spdlog::info("Playback reached end of mix");
                        m_playbackController->stop();
                        stopTimer();
                        m_timeline.setMixPlaybackPosition(-1.0); // Hide the red playhead
                    }
                }
                else
                {
                    // Not playing but still in mix mode - hide playhead but keep timer for responsiveness
                    m_timeline.setMixPlaybackPosition(-1.0);
                }
            }
            else
            {
                // Not in mix mode - stop timer and hide playhead
                stopTimer();
                m_timeline.setMixPlaybackPosition(-1.0);
            }
        }
    } // namespace ui
} // namespace jucyaudio