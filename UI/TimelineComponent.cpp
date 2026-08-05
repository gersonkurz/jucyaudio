#include <UI/MixEditorComponent.h>
#include <UI/Settings.h>
#include <UI/TimelineComponent.h>
#include <UI/PlaybackController.h>
#include <limits>
#include <spdlog/spdlog.h>
#include <toml++/toml.h> // Include the parser implementation here

namespace jucyaudio
{
    namespace ui
    {
        TimelineComponent::TimelineComponent(juce::AudioFormatManager &formatManager, juce::AudioThumbnailCache &thumbnailCache)
            : m_mixLoader{nullptr},
              m_formatManager{formatManager},
              m_thumbnailCache{thumbnailCache}
        {
            spdlog::info("[Timeline] Constructor called");
            setWantsKeyboardFocus(true);
            setInterceptsMouseClicks(true, true); // Make sure we receive mouse clicks
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
                // Check if we're in the middle of a drag operation
                if (m_isDraggingTrackForReorder)
                {
                    spdlog::info("Escape key pressed - canceling track reorder drag");
                    m_isDraggingTrackForReorder = false;
                    m_draggedTrackForReorder = nullptr;
                    m_dropTargetOrderInMix = -1;
                    repaint(); // Clear drop indicator
                    return true;
                }

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
                if (!m_isReadOnly && m_selectedTrack)
                {
                    // This now calls the corrected method below
                    deleteSelectedTrack();
                    return true; 
                }
                else if (m_isReadOnly)
                {
                    spdlog::info("Cannot delete track - mix is read-only (exported/locked)");
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

            // --- FIX: Get the playback controller from the parent editor ---
            auto* editor = findParentComponentOfClass<MixEditorComponent>();
            if (!editor)
            {
                spdlog::error("Could not find parent MixEditorComponent. Cannot stop playback engine.");
                return false;
            }
            auto* playbackController = editor->getPlaybackController();
            if (!playbackController)
            {
                spdlog::error("Could not get PlaybackController from editor.");
                return false;
            }

            // 2. Get identity of the concrete track instance to delete
            const auto trackIdToRemove{m_selectedTrack->getTrackId()};
            const auto orderInMixToRemove{m_selectedTrack->getOrderInMix()};
            const auto currentMixId{m_mixLoader->getMixId()};
            spdlog::info("Attempting to delete Track ID: {} (order {}) from Mix ID: {}", trackIdToRemove, orderInMixToRemove, currentMixId);

            // 3. Check if we need to show a confirmation dialog (logic is unchanged)
            bool shouldRemoveFromWorkingSet = false;

            if (config::theSettings.mixEditingSettings.removeFromWorkingSetOnDelete.get())
            {
                const auto &mixTracks = m_mixLoader->getMixTracks();
                int trackOccurrences = 0;
                for (const auto &mixTrack : mixTracks)
                {
                    if (mixTrack.trackId == trackIdToRemove)
                    {
                        trackOccurrences++;
                    }
                }
                if (trackOccurrences == 1)
                {
                    const auto &mixInfo = m_mixLoader->getMixInfo();
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
                            return false;
                        }
                        else if(usersChoice == config::RemoveTrackOption::RemoveFromBoth)
                        {
                            shouldRemoveFromWorkingSet = true;
                        }
                    }
                }
            }

            // --- FIX: Stop playback BEFORE any data model changes ---
            const bool wasPlaying = playbackController->isPlaying();
            double playbackPosition = 0.0;

            if (wasPlaying)
            {
                playbackPosition = playbackController->getCurrentPositionSeconds();
                spdlog::debug("TimelineComponent: Was playing at position {}. Stopping playback.", playbackPosition);
                playbackController->stop();
                juce::Thread::sleep(50); // Give audio thread a moment to stop
            }

            // 4. Remove exactly the selected row from the mix
            auto mixTracks = m_mixLoader->getMixTracks();
            const auto beforeCount = mixTracks.size();
            mixTracks.erase(std::remove_if(mixTracks.begin(), mixTracks.end(),
                [orderInMixToRemove](const database::MixTrack& track)
                {
                    return track.orderInMix == orderInMixToRemove;
                }),
                mixTracks.end());

            if (mixTracks.size() + 1 != beforeCount)
            {
                spdlog::error("Failed to find selected track instance at order {} for deletion", orderInMixToRemove);
                if (wasPlaying) playbackController->play();
                return false;
            }

            for (int i = 0; i < static_cast<int>(mixTracks.size()); ++i)
            {
                mixTracks[i].orderInMix = i;
            }

            auto mixInfo = m_mixLoader->getMixInfo();
            if (!theTrackLibrary.getMixManager().createOrUpdateMix(mixInfo, mixTracks))
            {
                spdlog::error("Failed to persist mix after track deletion.");
                // Attempt to restart playback if it was active
                if (wasPlaying) playbackController->play();
                return false;
            }

            // 5. If we should also remove from working set, do it now
            if (shouldRemoveFromWorkingSet)
            {
                const auto &mixInfo = m_mixLoader->getMixInfo();
                if (theTrackLibrary.getWorkingSetManager().removeTrackFromWorkingSet(mixInfo.source_ws_id, trackIdToRemove))
                {
                    spdlog::info("Also removed track {} from working set {}", trackIdToRemove, mixInfo.source_ws_id);
                }
            }

            // 6. Reload the updated mix data from the database so recalculated
            // adjacencies/attach points are reflected in the live timeline.
            if (!m_mixLoader->reloadFromDatabase())
            {
                spdlog::error("Failed to reload MixProjectLoader from database after deletion!");
                return false;
            }

            playbackPosition = std::min(
                playbackPosition,
                std::chrono::duration<double>(m_mixLoader->calculateMixDuration()).count());

            // --- FIX: Reload the now-modified mix into the playback engine ---
            spdlog::debug("TimelineComponent: Reloading mix in playback controller.");
            bool loadSuccess = playbackController->loadMix(m_mixLoader);

            // --- FIX: Resume playback if it was active before ---
            if (wasPlaying && loadSuccess)
            {
                spdlog::debug("TimelineComponent: Resuming playback from position {}", playbackPosition);
                playbackController->playMixFrom(playbackPosition);
            }

            // 7. Refresh the existing UI incrementally so we don't recreate thumbnails.
            refreshAfterDeletion(orderInMixToRemove);

            // Track count/duration changed - refresh the node summary and status bar.
            if (onMixSummaryChanged)
                onMixSummaryChanged();

            // Note: do NOT fire onMixChanged here. The createOrUpdateMix call above
            // already persisted the change and recorded an undo state. Firing onMixChanged
            // would trigger saveMixChanges, which calls createOrUpdateMix a second time,
            // pushing a duplicate undo state (requiring two Ctrl+Z to undo one delete).

            return true;
        }

        void TimelineComponent::copySelectedTrackToClipboard()
        {
            if (!m_selectedTrack || !m_mixLoader)
            {
                spdlog::warn("copySelectedTrackToClipboard: No track selected or no mix loaded");
                return;
            }

            // Find the track data in our views
            for (const auto &view : m_trackViews)
            {
                if (view.mixTrackData && view.trackInfoData && view.component.get() == m_selectedTrack)
                {
                    // Copy the data to clipboard
                    m_clipboard.mixTrack = *view.mixTrackData;
                    m_clipboard.trackInfo = *view.trackInfoData;
                    m_clipboard.isValid = true;

                    spdlog::info("Copied track {} (order {}) to clipboard", view.mixTrackData->trackId, view.mixTrackData->orderInMix);
                    return;
                }
            }

            spdlog::error("Failed to find selected track data for clipboard copy");
        }

        void TimelineComponent::cutSelectedTrackToClipboard()
        {
            if (m_isReadOnly)
            {
                spdlog::info("Cannot cut track - mix is read-only (exported/locked)");
                return;
            }
            
            if (!m_selectedTrack || !m_mixLoader)
            {
                spdlog::warn("cutSelectedTrackToClipboard: No track selected or no mix loaded");
                return;
            }

            // First copy to clipboard
            copySelectedTrackToClipboard();

            // Then remove the track from mix only (not from working set)
            if (m_clipboard.isValid)
            {
                const auto trackId = m_selectedTrack->getTrackId();
                const auto orderInMix = m_selectedTrack->getOrderInMix();
                if (removeTrackFromMixOnly(orderInMix))
                {
                    spdlog::info("Cut track {} (order {}) to clipboard", trackId, orderInMix);
                }
            }
        }

        void TimelineComponent::pasteFromClipboard(bool insertBefore)
        {
            if (m_isReadOnly)
            {
                spdlog::info("Cannot paste track - mix is read-only (exported/locked)");
                return;
            }
            
            if (!m_clipboard.isValid || !m_mixLoader)
            {
                spdlog::warn("pasteFromClipboard: No valid clipboard data or no mix loaded");
                return;
            }

            // Get the selected track index (or use end if nothing selected)
            int insertIndex = -1;
            if (m_selectedTrack)
            {
                // Find which track view corresponds to the selected component
                // We need to match by component pointer, not trackId, since we can have duplicates
                for (int i = 0; i < static_cast<int>(m_trackViews.size()); ++i)
                {
                    if (m_trackViews[i].component.get() == m_selectedTrack)
                    {
                        insertIndex = insertBefore ? i : i + 1;
                        spdlog::info("Found selected track at index {}, will insert at {}", i, insertIndex);
                        break;
                    }
                }

                if (insertIndex < 0)
                {
                    // Fallback: couldn't find the component in our views
                    spdlog::warn("Selected track component not found in track views");
                    insertIndex = static_cast<int>(m_mixLoader->getMixTracks().size());
                }
            }
            else
            {
                // No selection - paste at end
                insertIndex = static_cast<int>(m_mixLoader->getMixTracks().size());
            }

            if (insertIndex < 0)
            {
                spdlog::error("pasteFromClipboard: Failed to determine insert position");
                return;
            }

            // Create a new mix track list with the pasted track inserted
            auto mixTracks = m_mixLoader->getMixTracks();
            auto mixTrackToInsert = m_clipboard.mixTrack;

            // When pasting, we need to set up reasonable defaults for the new track position
            // The track keeps its envelope and relative cue/attach points, but we may need to
            // adjust timing based on where it's being inserted

            // Default crossfade duration
            const Duration_t defaultCrossfade{5000}; // 5 seconds

            if (insertIndex == 0 && !mixTracks.empty())
            {
                // Inserting at the beginning before all tracks
                // Set up to crossfade into the first track
                //const auto &nextTrack = mixTracks[0];
                const auto trackDuration = m_clipboard.trackInfo.duration;

                // Set attachTo to crossfade into next track's attachFrom
                mixTrackToInsert.attachTo = trackDuration - defaultCrossfade;

                // Keep cue points from clipboard or use defaults
                if (mixTrackToInsert.cueStart == Duration_t{0} && mixTrackToInsert.cueEnd == Duration_t{0})
                {
                    mixTrackToInsert.cueStart = Duration_t{0};
                    mixTrackToInsert.cueEnd = Duration_t{0};
                }
            }
            else if (insertIndex >= static_cast<int>(mixTracks.size()))
            {
                // Appending at the end
                if (!mixTracks.empty())
                {
                    // Set up to crossfade from the last track
                    mixTrackToInsert.attachFrom = defaultCrossfade;
                    mixTrackToInsert.attachTo = m_clipboard.trackInfo.duration;
                }
            }
            else
            {
                // Inserting in the middle
                // Set up crossfades with neighbors
                if (insertIndex > 0)
                {
                    // Has a previous track - set attachFrom for crossfade
                    mixTrackToInsert.attachFrom = defaultCrossfade;
                }

                if (insertIndex < static_cast<int>(mixTracks.size()))
                {
                    // Has a next track - set attachTo for crossfade
                    mixTrackToInsert.attachTo = m_clipboard.trackInfo.duration - defaultCrossfade;
                }
            }

            // Insert at the specified position
            if (insertIndex >= static_cast<int>(mixTracks.size()))
            {
                // Add at end
                mixTracks.push_back(mixTrackToInsert);
            }
            else
            {
                // Insert at specific position
                mixTracks.insert(mixTracks.begin() + insertIndex, mixTrackToInsert);
            }

            // Renumber the orderInMix for all tracks
            for (int i = 0; i < static_cast<int>(mixTracks.size()); ++i)
            {
                mixTracks[i].orderInMix = i;
            }

            // Save the updated mix using createOrUpdateMix
            auto mixInfo = m_mixLoader->getMixInfo();
            //const auto currentMixId = m_mixLoader->getMixId();

            if (theTrackLibrary.getMixManager().createOrUpdateMix(mixInfo, mixTracks))
            {
                spdlog::info("Pasted track {} from clipboard at position {}", mixTrackToInsert.trackId, insertIndex);

                // Reload from database to get the updated mix
                if (m_mixLoader->reloadFromDatabase())
                {
                    // Refresh the UI
                    populateFrom();

                    if (onMixPlaybackReloadRequested)
                    {
                        onMixPlaybackReloadRequested();
                    }

                    if (onMixChanged)
                    {
                        onMixChanged();
                    }
                }
                else
                {
                    spdlog::error("Failed to reload mix after paste");
                }
            }
            else
            {
                spdlog::error("Failed to paste track from clipboard");
            }
        }

        void TimelineComponent::removeAllTracksAfterSelected()
        {
            if (!m_selectedTrack || !m_mixLoader)
            {
                spdlog::warn("removeAllTracksAfterSelected: No track selected or no mix loaded");
                return;
            }

            // --- FIX: Get the playback controller from the parent editor ---
            auto* editor = findParentComponentOfClass<MixEditorComponent>();
            if (!editor)
            {
                spdlog::error("Could not find parent MixEditorComponent. Cannot stop playback engine.");
                return;
            }
            auto* playbackController = editor->getPlaybackController();
            if (!playbackController)
            {
                spdlog::error("Could not get PlaybackController from editor.");
                return;
            }

            // Find the selected track's orderInMix. This is robust against duplicate track IDs.
            const int selectedOrder = m_selectedTrack->getOrderInMix();
            const auto &mixTracks = m_mixLoader->getMixTracks();

            auto updatedMixTracks = mixTracks;
            const auto oldCount = updatedMixTracks.size();
            updatedMixTracks.erase(
                std::remove_if(updatedMixTracks.begin(), updatedMixTracks.end(),
                    [selectedOrder](const database::MixTrack& track)
                    {
                        return track.orderInMix > selectedOrder;
                    }),
                updatedMixTracks.end());

            const auto removedCount = oldCount - updatedMixTracks.size();
            if (removedCount == 0)
            {
                spdlog::info("removeAllTracksAfterSelected: No tracks to remove after selected track");
                return;
            }

            spdlog::info("Removing {} tracks after order {} (from mix only, keeping in working set)", removedCount, selectedOrder);

            for (int i = 0; i < static_cast<int>(updatedMixTracks.size()); ++i)
            {
                updatedMixTracks[i].orderInMix = i;
            }

            // --- FIX: Stop playback BEFORE any data model changes ---
            const bool wasPlaying = playbackController->isPlaying();
            double playbackPosition = 0.0;

            if (wasPlaying)
            {
                playbackPosition = playbackController->getCurrentPositionSeconds();
                spdlog::debug("TimelineComponent: Was playing at position {}. Stopping playback.", playbackPosition);
                playbackController->stop();
                juce::Thread::sleep(50); // Give audio thread a moment to stop
            }

            auto mixInfo = m_mixLoader->getMixInfo();
            if (theTrackLibrary.getMixManager().createOrUpdateMix(mixInfo, updatedMixTracks))
            {
                spdlog::info("Successfully removed tracks from database.");
                // Reload from database to get the updated mix
                if (m_mixLoader->reloadFromDatabase())
                {
                    // --- FIX: Reload the now-modified mix into the playback engine ---
                    spdlog::debug("TimelineComponent: Reloading mix in playback controller.");
                    bool loadSuccess = playbackController->loadMix(m_mixLoader);

                    // Refresh the UI
                    populateFrom();

                    // Note: do NOT fire onMixChanged here. The createOrUpdateMix call above
                    // already persisted the change and recorded an undo state.

                    // --- FIX: Resume playback if it was active before ---
                    if (wasPlaying && loadSuccess)
                    {
                        spdlog::debug("TimelineComponent: Resuming playback from position {}", playbackPosition);
                        playbackController->playMixFrom(playbackPosition);
                    }
                }
                else
                {
                    spdlog::error("Failed to reload mix after removing tracks");
                }
            }
            else
            {
                spdlog::error("Failed to persist mix after removing tracks after order {}", selectedOrder);
            }
        }

        bool TimelineComponent::removeTrackFromMixOnly(int orderInMixToRemove)
        {
            if (!m_mixLoader)
            {
                spdlog::error("removeTrackFromMixOnly called but m_mixLoader is null.");
                return false;
            }

            const auto currentMixId = m_mixLoader->getMixId();
            spdlog::info("Removing track at order {} from mix {} (mix only, not working set)", orderInMixToRemove, currentMixId);

            auto mixTracks = m_mixLoader->getMixTracks();
            const auto beforeCount = mixTracks.size();

            mixTracks.erase(std::remove_if(mixTracks.begin(), mixTracks.end(),
                [orderInMixToRemove](const database::MixTrack& track)
                {
                    if (track.orderInMix == orderInMixToRemove)
                    {
                        return true;
                    }
                    return false;
                }),
                mixTracks.end());

            if (mixTracks.size() + 1 != beforeCount)
            {
                spdlog::error("Failed to find track at order {} for removal.", orderInMixToRemove);
                return false;
            }

            for (int i = 0; i < static_cast<int>(mixTracks.size()); ++i)
            {
                mixTracks[i].orderInMix = i;
            }

            auto mixInfo = m_mixLoader->getMixInfo();
            if (!theTrackLibrary.getMixManager().createOrUpdateMix(mixInfo, mixTracks))
            {
                spdlog::error("Failed to persist mix after removing track at order {}.", orderInMixToRemove);
                return false;
            }

            // Clear selection if we removed the selected component
            if (m_selectedTrack && m_selectedTrack->getOrderInMix() == orderInMixToRemove)
            {
                m_selectedTrack = nullptr;
            }

            // Reload and refresh UI
            if (!m_mixLoader->reloadFromDatabase())
            {
                spdlog::error("Failed to reload MixProjectLoader from database after removal.");
                return false;
            }

            populateFrom();

            if (onMixPlaybackReloadRequested)
            {
                onMixPlaybackReloadRequested();
            }

            // Track count/duration changed - refresh the node summary and status bar.
            if (onMixSummaryChanged)
                onMixSummaryChanged();

            // Note: do NOT fire onMixChanged here. The createOrUpdateMix call above
            // already persisted the change and recorded an undo state.

            return true;
        }

        void TimelineComponent::deleteTrackAtIndex(size_t trackIndex)
        {
            if (!m_mixLoader || trackIndex >= m_mixLoader->getMixTracks().size())
            {
                spdlog::error("deleteTrackAtIndex: Invalid index or no mix loaded");
                return;
            }

            const auto &mixTracks = m_mixLoader->getMixTracks();
            const auto orderInMix = mixTracks[trackIndex].orderInMix;
            if (removeTrackFromMixOnly(orderInMix))
            {
                spdlog::info("Deleted track at index {}", trackIndex);
            }
            else
            {
                spdlog::error("Failed to delete track at index {}", trackIndex);
            }
        }

        void TimelineComponent::refreshAfterDeletion(int deletedOrderInMix)
        {
            if (!m_mixLoader)
            {
                spdlog::error("TimelineComponent::refreshAfterDeletion - No mix loader. Falling back to full populate.");
                populateFrom();
                return;
            }

            // Step 1: Remove the UI component and its view from our list
            auto it = std::find_if(m_trackViews.begin(),
                m_trackViews.end(),
                [deletedOrderInMix](const TrackView &view)
                {
                    return view.component && view.component->getOrderInMix() == deletedOrderInMix;
                });

            if (it != m_trackViews.end())
            {
                if (m_selectedTrack == it->component.get())
                {
                    m_selectedTrack = nullptr;
                }
                m_trackViews.erase(it);
                spdlog::info("Removed TrackView for orderInMix {}", deletedOrderInMix);
            }
            else
            {
                spdlog::warn("Could not find TrackView for deleted orderInMix {}. Performing full repopulation.", deletedOrderInMix);
                populateFrom();
                return;
            }

            // Step 2: Update data pointers using the old order indices so duplicate track IDs remain stable.
            const auto &newMixTracks = m_mixLoader->getMixTracks();

            for (auto &view : m_trackViews)
            {
                const auto oldOrder = view.component->getOrderInMix();
                const auto newOrder = oldOrder > deletedOrderInMix ? oldOrder - 1 : oldOrder;
                if (newOrder >= 0 && newOrder < static_cast<int>(newMixTracks.size()))
                {
                    view.mixTrackData = const_cast<database::MixTrack *>(&newMixTracks[static_cast<size_t>(newOrder)]);
                    view.trackInfoData = m_mixLoader->getTrackInfoForId(view.mixTrackData->trackId);
                    if (!view.trackInfoData)
                    {
                        spdlog::error("Missing TrackInfo in refreshAfterDeletion for track {}. Falling back to full populate.", view.mixTrackData->trackId);
                        populateFrom();
                        return;
                    }
                }
                else
                {
                    spdlog::error("Inconsistent state in refreshAfterDeletion for old order {}. Falling back to full populate.", oldOrder);
                    populateFrom();
                    return;
                }
            }

            // Re-sort m_trackViews to match the new orderInMix from the reloaded data
            std::sort(m_trackViews.begin(),
                m_trackViews.end(),
                [](const TrackView &a, const TrackView &b)
                {
                    return a.mixTrackData->orderInMix < b.mixTrackData->orderInMix;
                });

            spdlog::info("Successfully updated data pointers and re-sorted {} remaining tracks.", m_trackViews.size());

            // Step 3: Recalculate all positions and refresh the layout
            recalculateTrackPositions();
            spdlog::info("Recalculated positions and refreshed layout after deletion.");
        }

        void TimelineComponent::paint(juce::Graphics &g)
        {
            // Track paint frequency
            static int paintCount = 0;
            static auto lastReportTime = std::chrono::high_resolution_clock::now();
            paintCount++;
            
            auto now = std::chrono::high_resolution_clock::now();
            auto timeSinceReport = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReportTime);
            if (timeSinceReport.count() >= 1000)
            {
                spdlog::debug("TimelineComponent paint calls: {} in last second", paintCount);
                paintCount = 0;
                lastReportTime = now;
            }
            
            // Draw the main background
            g.fillAll(getLookAndFeel().findColour(juce::TreeView::backgroundColourId));

            // OPTIMIZATION: Only draw grid lines that are visible in the viewport
            // Get the clip bounds - this is the area that actually needs painting
            auto clipBounds = g.getClipBounds();
            const float clipLeft = static_cast<float>(clipBounds.getX());
            const float clipRight = static_cast<float>(clipBounds.getRight());
            
            // Draw the time grid - but only for the visible area
            const auto gridLineColour = getLookAndFeel().findColour(juce::TextEditor::outlineColourId);
            const auto textColour = getLookAndFeel().findColour(juce::Label::textColourId);
            
            // Calculate the first and last visible grid lines
            const float gridSpacing = 30.0f * m_pixelsPerSecond; // 30-second intervals
            const int firstGridLine = std::max(0, static_cast<int>(clipLeft / gridSpacing));
            const int lastGridLine = static_cast<int>(clipRight / gridSpacing) + 1;
            
            for (int i = firstGridLine; i <= lastGridLine; ++i)
            {
                const float x = i * gridSpacing;
                
                // Only draw if actually in the clip region
                if (x >= clipLeft - 1 && x <= clipRight + 1)
                {
                    g.setColour(gridLineColour);
                    g.drawVerticalLine(juce::roundToInt(x), 0.0f, static_cast<float>(getHeight()));

                    int minutes = (i * 30) / 60;
                    const int seconds = (i * 30) % 60;
                    const int hours = minutes / 60;
                    minutes %= 60;
                    juce::String time = juce::String::formatted("%d:%02d:%02d", hours, minutes, seconds);
                    g.setColour(textColour);
                    g.drawText(time, juce::roundToInt(x) + 4, 4, 100, 20, juce::Justification::topLeft);
                }
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

            // Playhead is now drawn by PlayheadOverlay component for better performance

            // Draw cue drag preview line (dashed orange line)
            if (m_cueDragPreviewTime.has_value())
            {
                const auto previewTimeSeconds = std::chrono::duration<double>(*m_cueDragPreviewTime).count();
                const float previewX = static_cast<float>(previewTimeSeconds * m_pixelsPerSecond);

                g.setColour(juce::Colours::orange.withAlpha(0.8f));

                // Draw a dashed line
                float dashLengths[] = {4.0f, 4.0f};
                juce::Line<float> previewLine(previewX, 0.0f, previewX, static_cast<float>(getHeight()));
                g.drawDashedLine(previewLine, dashLengths, 2);
            }

            // Draw drag-and-drop visual feedback for track reordering
            if (m_isDraggingTrackForReorder && m_draggedTrackForReorder)
            {
                // 1. Draw floating rectangle that follows the mouse cursor (both X and Y)
                auto originalBounds = m_draggedTrackForReorder->getBounds();

                // Calculate offset from drag start to current position
                int xOffset = m_currentDragPosition.x - m_trackDragStartPosition.x;
                int yOffset = m_currentDragPosition.y - m_trackDragStartPosition.y;

                // Create floating rectangle at new position (moves both X and Y with mouse)
                juce::Rectangle<int> floatingRect(
                    originalBounds.getX() + xOffset,
                    originalBounds.getY() + yOffset,
                    originalBounds.getWidth(),
                    originalBounds.getHeight()
                );

                // Draw semi-transparent floating track
                g.setColour(juce::Colours::orange.withAlpha(0.5f));
                g.fillRect(floatingRect);
                g.setColour(juce::Colours::orange.withAlpha(0.9f));
                g.drawRect(floatingRect, 3); // 3px border for emphasis

                // Dim the original position slightly to show it's "lifted"
                g.setColour(juce::Colours::black.withAlpha(0.2f));
                g.fillRect(originalBounds);

                // 2. Draw drop indicator - highlight target track and show insertion line
                if (m_dropTargetOrderInMix >= 0)
                {
                    // Find and highlight the target track
                    for (const auto &view : m_trackViews)
                    {
                        if (view.mixTrackData->orderInMix == m_dropTargetOrderInMix)
                        {
                            auto targetBounds = view.component->getBounds();

                            // Draw semi-transparent highlight over target track
                            g.setColour(juce::Colours::cyan.withAlpha(0.3f));
                            g.fillRect(targetBounds);

                            // Draw border around target track
                            g.setColour(juce::Colours::cyan.withAlpha(0.8f));
                            g.drawRect(targetBounds, 2);

                            // Draw horizontal insertion line at top of target track
                            g.setColour(juce::Colours::orange.withAlpha(0.9f));
                            g.fillRect(0, targetBounds.getY() - 2, getWidth(), 4);
                            break;
                        }
                    }
                }
            }
        }

        void TimelineComponent::refreshLayout()
        {
            // Set flag to force resized() to recalculate track bounds
            m_zoomHasChanged = true;

            double maxTimeSecs = 0.0;
            for (const auto &view : m_trackViews)
            {
                // The start position is dynamic.
                const double startTime = std::chrono::duration<double>(view.componentStartTime).count();

                // The duration is dynamic.
                const double effectiveDuration = std::chrono::duration<double>(view.mixTrackData->getEffectiveDuration(view.trackInfoData->duration)).count();

                const double endTime = startTime + effectiveDuration;
                maxTimeSecs = std::max(maxTimeSecs, endTime);

                // Update the zoom level in each track component
                view.component->setPixelsPerSecond(m_pixelsPerSecond);
            }

            m_calculatedWidth = static_cast<int>(maxTimeSecs * m_pixelsPerSecond) + 200;
            setSize(m_calculatedWidth, m_calculatedHeight);

            resized();
            repaint();
        }

        void TimelineComponent::repositionTrack(TrackId trackId)
        {
            spdlog::debug("[REPOSITION] repositionTrack called for TrackId={}", trackId);

            // Find which track index this is
            int trackIndex = -1;
            for (size_t i = 0; i < m_trackViews.size(); ++i)
            {
                if (m_trackViews[i].mixTrackData && m_trackViews[i].mixTrackData->trackId == trackId)
                {
                    trackIndex = static_cast<int>(i);
                    spdlog::debug("[REPOSITION]   Found track at index {}, OrderInMix={}", i, m_trackViews[i].mixTrackData->orderInMix);
                    spdlog::debug("[REPOSITION]   Current values: CueStart={}ms, CueEnd={}ms, AttachFrom={}ms, AttachTo={}ms",
                                m_trackViews[i].mixTrackData->cueStart.count(),
                                m_trackViews[i].mixTrackData->cueEnd.count(),
                                m_trackViews[i].mixTrackData->attachFrom.count(),
                                m_trackViews[i].mixTrackData->attachTo.count());
                    break;
                }
            }

            if (trackIndex == -1)
            {
                spdlog::warn("[REPOSITION] Track {} not found in track views", trackId);
                return;
            }

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

            spdlog::debug("[REPOSITION] Needs full recalculation: {}", needsFullRecalculation);

            if (needsFullRecalculation)
            {
                // Recalculate all positions without recreating components
                recalculateTrackPositions();
            }
            else
            {
                // For the last track with only cueStart/cueEnd changes, just update that track
                auto &view = m_trackViews[trackIndex];
                view.componentStartTime = view.audioStartTime + view.mixTrackData->cueStart;

                spdlog::debug("[REPOSITION] Updated last track only - componentStartTime={}ms", view.componentStartTime.count());

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

            spdlog::debug("[RECALC] recalculateTrackPositions - Processing {} tracks", m_trackViews.size());

            // Calculate the global offset from the first track's cueStart
            Duration_t globalOffset{0};
            if (m_trackViews[0].mixTrackData && m_trackViews[0].mixTrackData->cueStart < Duration_t{0})
            {
                globalOffset = -m_trackViews[0].mixTrackData->cueStart;
                spdlog::debug("[RECALC]   First track has negative cueStart, globalOffset={}ms", globalOffset.count());
            }

            // Recalculate positions for all tracks
            Duration_t previousAudioStartTime{0};

            for (size_t i = 0; i < m_trackViews.size(); ++i)
            {
                auto &view = m_trackViews[i];
                if (!view.mixTrackData)
                    continue;

                // Calculate audio start time according to Mix Flow algorithm
                if (i == 0)
                {
                    view.audioStartTime = globalOffset;
                }
                else
                {
                    const auto &prevTrack = *m_trackViews[i - 1].mixTrackData;
                    view.audioStartTime = previousAudioStartTime + prevTrack.attachTo - view.mixTrackData->attachFrom;
                }

                // Update component start time
                view.componentStartTime = view.audioStartTime + view.mixTrackData->cueStart;

                spdlog::debug("[RECALC]   Track {} (OrderInMix={}): CueStart={}ms, AttachFrom={}ms, AttachTo={}ms, audioStartTime={}ms, componentStartTime={}ms",
                            view.mixTrackData->trackId, view.mixTrackData->orderInMix,
                            view.mixTrackData->cueStart.count(),
                            view.mixTrackData->attachFrom.count(),
                            view.mixTrackData->attachTo.count(),
                            view.audioStartTime.count(),
                            view.componentStartTime.count());

                previousAudioStartTime = view.audioStartTime;
            }

            // Refresh the layout with the new positions
            spdlog::debug("[RECALC] Calling refreshLayout()");
            refreshLayout();

            // Synchronize all MixTrackComponent internal data with the updated MixProjectLoader data
            // This prevents stale data from being used when components fire callbacks
            spdlog::debug("[RECALC] Synchronizing component data after recalculation");
            for (auto &view : m_trackViews)
            {
                if (view.component && view.mixTrackData)
                {
                    view.component->updateMixTrackData(*view.mixTrackData);
                }
            }
        }

        void TimelineComponent::maintainViewportPosition(double timeAtMouse, int mouseX)
        {
            if (auto *viewport = findParentComponentOfClass<juce::Viewport>()) // JUCE_API
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
                event.position.x,
                event.position.y,
                event.getNumberOfClicks(),
                event.mods.isLeftButtonDown());

            // Always grab keyboard focus when the timeline is clicked.
            grabKeyboardFocus();

            if (event.mods.isLeftButtonDown())
            {
                // Check if clicking on a track (for drag-and-drop reordering)
                // Note: MixTrackComponent already filters out interactive elements (markers, envelope points)
                // and only forwards clicks from non-interactive areas, so any click we receive here
                // on a track is safe to treat as a potential reorder drag
                // BUT: Don't initiate drag on double-clicks - those are for playback
                if (!m_isReadOnly && event.getNumberOfClicks() == 1)
                {
                    if (const auto clickedTrack = getTrackAtPosition(event.position.toInt()))
                    {
                        spdlog::info("[Timeline] Initiating drag for track reorder");
                        m_isDraggingTrackForReorder = true;
                        m_draggedTrackForReorder = clickedTrack;
                        m_currentDragPosition = m_trackDragStartPosition = event.position.toInt();
                        m_dropTargetOrderInMix = -1;
                        setSelectedTrack(clickedTrack);
                        return; // Don't process as normal click
                    }
                }

                // Convert the pixel x-coordinate to a time in seconds.
                double clickTime = event.position.x / m_pixelsPerSecond;
                spdlog::info("[Timeline] Click time: {} seconds (x={}, pixelsPerSecond={})", clickTime, event.position.x, m_pixelsPerSecond);

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

        void TimelineComponent::mouseDrag(const juce::MouseEvent &event)
        {
            if (m_isDraggingTrackForReorder && m_draggedTrackForReorder)
            {
                // Update current drag position for floating rectangle
                m_currentDragPosition = event.position.toInt();

                // Find which track is under the current mouse position (full X,Y bounds check).
                // This prevents accidental reorders from a click/jitter — you must actually drag
                // onto a different track's visual area to arm the reorder. (Forward-ported from the
                // 1.x fix; 2.0's pointToOrderInMix had an x-closest fallback that always returned a
                // track, which reintroduced the accidental-reorder bug.)
                auto* targetTrack = getTrackAtPosition(event.position.toInt());

                if (targetTrack && targetTrack != m_draggedTrackForReorder)
                {
                    // Mouse is over a different track - find its orderInMix
                    for (const auto& view : m_trackViews)
                    {
                        if (view.component.get() == targetTrack)
                        {
                            if (view.mixTrackData->orderInMix != m_dropTargetOrderInMix)
                            {
                                m_dropTargetOrderInMix = view.mixTrackData->orderInMix;
                                spdlog::info("[Timeline] Drag target updated to orderInMix: {}", m_dropTargetOrderInMix);
                            }
                            break;
                        }
                    }
                }
                else if (!targetTrack)
                {
                    // Mouse is not over any track - clear the drop target
                    m_dropTargetOrderInMix = -1;
                }
                // If targetTrack == m_draggedTrackForReorder, don't change m_dropTargetOrderInMix
                // (keep previous target if we had one, or -1 if we didn't)

                repaint(); // Repaint to update both floating rectangle and drop indicator
            }
        }

        void TimelineComponent::mouseUp(const juce::MouseEvent &event)
        {
            if (m_isDraggingTrackForReorder && m_draggedTrackForReorder && m_dropTargetOrderInMix >= 0)
            {
                // Get the current row position from the dragged track component.
                int currentOrder = -1;

                for (size_t i = 0; i < m_trackViews.size(); ++i)
                {
                    if (m_trackViews[i].component.get() == m_draggedTrackForReorder)
                    {
                        currentOrder = m_trackViews[i].mixTrackData->orderInMix;
                        break;
                    }
                }

                if (currentOrder >= 0 && currentOrder != m_dropTargetOrderInMix && m_mixLoader)
                {
                    spdlog::info("[Timeline] Executing reorder from order {} to {}",
                                currentOrder, m_dropTargetOrderInMix);

                    // Get the mix manager and execute the reorder
                    const auto mixId = m_mixLoader->getMixInfo().mixId;

                    if (theTrackLibrary.getMixManager().reorderTrackInMix(mixId, currentOrder, m_dropTargetOrderInMix))
                    {
                        spdlog::info("[Timeline] Track reordered successfully, scheduling reload");

                        // IMPORTANT: We must defer the reload to avoid deleting components during their event handlers
                        // The MixTrackComponent that initiated this drag is still in its mouseUp handler,
                        // so we can't destroy it yet. Use MessageManager::callAsync to defer the reload.
                        juce::Component::SafePointer<TimelineComponent> safeThis = this;
                        juce::MessageManager::callAsync([safeThis]() {
                            if (!safeThis)
                                return;
                            if (safeThis->m_mixLoader)
                            {
                                spdlog::info("[Timeline] Reloading mix after reorder");
                                safeThis->m_mixLoader->reloadFromDatabase();
                                safeThis->populateFrom(safeThis->m_mixLoader);

                                // Trigger mix reload for audio engine if playing
                                if (safeThis->onMixPlaybackReloadRequested)
                                {
                                    safeThis->onMixPlaybackReloadRequested();
                                }
                            }
                        });
                    }
                    else
                    {
                        spdlog::error("[Timeline] Failed to reorder track in database");
                    }
                }
            }

            // Clear drag state
            m_isDraggingTrackForReorder = false;
            m_draggedTrackForReorder = nullptr;
            m_dropTargetOrderInMix = -1;
            repaint(); // Clear drop indicator
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

        int TimelineComponent::pointToOrderInMix(juce::Point<int> position) const
        {
            if (m_trackViews.empty())
                return -1;

            // If cursor is directly over a track component, use that (most precise)
            if (const auto trackAtPos = getTrackAtPosition(position))
            {
                for (const auto &view : m_trackViews)
                {
                    if (view.component.get() == trackAtPos)
                        return view.mixTrackData->orderInMix;
                }
            }

            // Not over a track - find the closest track by x-coordinate (time position)
            const double cursorTime = position.x / m_pixelsPerSecond;

            int closestOrder = -1;
            double closestDistance = std::numeric_limits<double>::max();

            for (const auto &view : m_trackViews)
            {
                const double startTime = std::chrono::duration<double>(view.componentStartTime).count();
                const double effectiveDuration = std::chrono::duration<double>(
                    view.mixTrackData->getEffectiveDuration(view.trackInfoData->duration)).count();
                const double midTime = startTime + effectiveDuration / 2.0;

                const double distance = std::abs(cursorTime - midTime);
                if (distance < closestDistance)
                {
                    closestDistance = distance;
                    closestOrder = view.mixTrackData->orderInMix;
                }
            }

            return closestOrder;
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
            constexpr double epsilon = 1e-9;
            if (std::abs(m_currentTimePosition - timeInSeconds) > epsilon)
            {
                // Dirty rectangle optimization: only repaint the old and new playhead positions
                const int playheadWidth = 3; // Width of the playhead line plus margin
                
                // Repaint old position to clear it
                if (m_currentTimePosition >= 0.0)
                {
                    const int oldX = static_cast<int>(m_currentTimePosition * m_pixelsPerSecond);
                    repaint(oldX - playheadWidth, 0, playheadWidth * 2, getHeight());
                }
                
                m_currentTimePosition = timeInSeconds;
                
                // Repaint new position to draw it
                if (m_currentTimePosition >= 0.0)
                {
                    const int newX = static_cast<int>(m_currentTimePosition * m_pixelsPerSecond);
                    repaint(newX - playheadWidth, 0, playheadWidth * 2, getHeight());
                }
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

            constexpr double epsilon = 1e-9;
            if (std::abs(newZoom - m_pixelsPerSecond) > epsilon)
            {
                m_pixelsPerSecond = newZoom;
                refreshLayout();

                // Keep the time position under the mouse cursor stable
                maintainViewportPosition(timeAtMouse, mousePos.x);
                
                // Notify parent of zoom change
                if (onZoomChanged)
                    onZoomChanged();
            }
        }

        void TimelineComponent::viewportResized()
        {
            // Only call resized if we actually need to recalculate
            // This is just a notification that viewport changed, but our internal layout might not need updating
            
            // Check if height actually changed enough to affect lanes
            const int rulerHeight = 30;
            const int trackHeight = MixTrackComponent::TOTAL_COMPONENT_HEIGHT;
            const int yGap = 5;
            
            if (auto *viewport = findParentComponentOfClass<juce::Viewport>())
            {
                const int viewportHeight = viewport->getHeight();
                const int availableHeightForLanes = viewportHeight - rulerHeight;
                const int numLanes = std::max(1, availableHeightForLanes / (trackHeight + yGap));
                
                // Only proceed if lanes would change (including first initialization when m_cachedNumLanes is -1)
                if (numLanes != m_cachedNumLanes)
                {
                    resized();
                }
                // Otherwise, skip the expensive resized() call entirely
            }
        }

        void TimelineComponent::resized()
        {
            // Performance logging
            const auto startTime = std::chrono::high_resolution_clock::now();
            
            //auto visibleArea = getParentComponent()->getLocalBounds();
            const int rulerHeight = 30;
            const int trackHeight = MixTrackComponent::TOTAL_COMPONENT_HEIGHT;
            const int yGap = 5;

            // Always match the viewport height if we have one
            if (auto *viewport = findParentComponentOfClass<juce::Viewport>()) // JUCE_API
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
            
            // OPTIMIZATION: Only recalculate track positions if the number of lanes changed
            // This avoids expensive recalculation during continuous window resizing
            // BUT: We must always recalculate if zoom has changed (m_zoomHasChanged flag)
            if (numLanes == m_cachedNumLanes && !m_zoomHasChanged)
            {
                // Number of lanes hasn't changed and zoom hasn't changed, no need to recalculate all track positions
                spdlog::debug("TimelineComponent::resized - Skipping (lanes unchanged: {}, zoom unchanged)", numLanes);
                return;
            }

            // Clear zoom change flag if it was set
            m_zoomHasChanged = false;
            
            spdlog::debug("TimelineComponent::resized - Recalculating {} tracks for {} lanes (was {})",
                        m_trackViews.size(), numLanes, m_cachedNumLanes);
            
            // Cache the new lane count
            m_cachedNumLanes = numLanes;
            
            int currentLane = 0;
            int laneDirection = +1;

            // Track statistics for performance analysis
            int initialLayoutCount = 0;
            int fastPathCount = 0;
            int fullRecalcCount = 0;
            
            const auto loopStartTime = std::chrono::high_resolution_clock::now();
            
            // Process tracks in batches to improve cache locality
            for (const auto &view : m_trackViews)
            {
                const int yPos = rulerHeight + (currentLane * (trackHeight + yGap));
                
                // OPTIMIZATION: For pure vertical resize (most common), only Y position changes
                // Skip expensive duration calculations if X and width won't change
                auto currentBounds = view.component->getBounds();
                
                // Check if this is initial layout (bounds would be 0,0,0,0)
                if (currentBounds.isEmpty())
                {
                    initialLayoutCount++;
                    // Initial layout - must calculate everything
                    const double startTime = std::chrono::duration<double>(view.componentStartTime).count();
                    const int startX = static_cast<int>(startTime * m_pixelsPerSecond);
                    const double effectiveDuration = std::chrono::duration<double>(view.mixTrackData->getEffectiveDuration(view.trackInfoData->duration)).count();
                    const int width = static_cast<int>(effectiveDuration * m_pixelsPerSecond);
                    spdlog::debug("TimelineComponent::resized - setting bounds for track {}: x={}, width={}, pixelsPerSecond={}",
                                 view.mixTrackData->orderInMix, startX, width, m_pixelsPerSecond);
                    view.component->setBounds(startX, yPos, width, trackHeight);
                }
                // Check if only Y position needs updating (common case for pure vertical resize).
                // After edits like deletion, X and width may also change, so validate them first.
                else if (currentBounds.getHeight() == trackHeight &&
                         currentBounds.getWidth() > 0)
                {
                    const double startTime = std::chrono::duration<double>(view.componentStartTime).count();
                    const int startX = static_cast<int>(startTime * m_pixelsPerSecond);
                    const double effectiveDuration = std::chrono::duration<double>(view.mixTrackData->getEffectiveDuration(view.trackInfoData->duration)).count();
                    const int width = static_cast<int>(effectiveDuration * m_pixelsPerSecond);

                    if (currentBounds.getX() == startX &&
                        currentBounds.getWidth() == width &&
                        currentBounds.getY() != yPos)
                    {
                        fastPathCount++;
                        // Fast path: just update Y position
                        view.component->setTopLeftPosition(currentBounds.getX(), yPos);
                    }
                    else
                    {
                        fullRecalcCount++;
                        if (currentBounds.getX() != startX ||
                            currentBounds.getY() != yPos ||
                            currentBounds.getWidth() != width ||
                            currentBounds.getHeight() != trackHeight)
                        {
                            view.component->setBounds(startX, yPos, width, trackHeight);
                        }
                    }
                }
                else
                {
                    fullRecalcCount++;
                    // Need to recalculate (zoom changed or size changed)
                    const double startTime = std::chrono::duration<double>(view.componentStartTime).count();
                    const int startX = static_cast<int>(startTime * m_pixelsPerSecond);
                    const double effectiveDuration = std::chrono::duration<double>(view.mixTrackData->getEffectiveDuration(view.trackInfoData->duration)).count();
                    const int width = static_cast<int>(effectiveDuration * m_pixelsPerSecond);
                    
                    if (currentBounds.getX() != startX || 
                        currentBounds.getY() != yPos || 
                        currentBounds.getWidth() != width ||
                        currentBounds.getHeight() != trackHeight)
                    {
                        view.component->setBounds(startX, yPos, width, trackHeight);
                    }
                }

                if ((currentLane + laneDirection) >= numLanes || (currentLane + laneDirection) < 0)
                    laneDirection *= -1;

                currentLane += laneDirection;
                if (numLanes == 1)
                    currentLane = 0;
            }
            
            // Performance timing report
            const auto loopEndTime = std::chrono::high_resolution_clock::now();
            
            const auto loopDuration = std::chrono::duration_cast<std::chrono::microseconds>(loopEndTime - loopStartTime);
            const auto totalDuration = std::chrono::duration_cast<std::chrono::microseconds>(loopEndTime - startTime);
            
            spdlog::debug("TimelineComponent::resized - Performance Report:");
            spdlog::debug("  Total tracks: {}", m_trackViews.size());
            spdlog::debug("  Initial layouts: {}, Fast path: {}, Full recalc: {}",
                        initialLayoutCount, fastPathCount, fullRecalcCount);
            spdlog::debug("  Track loop took: {} µs ({} µs/track)",
                        loopDuration.count(),
                        m_trackViews.empty() ? 0 : loopDuration.count() / m_trackViews.size());
            spdlog::debug("  Total resized() took: {} µs", totalDuration.count());
        }

        bool TimelineComponent::populateFrom(audio::MixProjectLoader *mixLoader)
        {
            m_isPopulating = true; // Set flag at the beginning

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
            m_cachedNumLanes = -1; // Reset lane cache when repopulating

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
                    view.component->setPixelsPerSecond(m_pixelsPerSecond);

                    view.component->onCueAttachChanged = [this](int orderInMix, const database::MixTrack &updatedTrack)
                    {
                        if (m_isReadOnly)
                        {
                            spdlog::info("Cannot modify cue/attach points - mix is read-only");
                            return;
                        }
                        if (!m_isPopulating && onCueAttachChanged)
                            onCueAttachChanged(orderInMix, updatedTrack);
                    };
                    view.component->onEnvelopeChanged = [this](int orderInMix, const std::vector<database::EnvelopePoint> &points)
                    {
                        if (m_isReadOnly)
                        {
                            spdlog::info("Cannot modify envelope - mix is read-only");
                            return;
                        }
                        if (!m_isPopulating && onEnvelopeChanged)
                            onEnvelopeChanged(orderInMix, points);
                    };
                    view.component->onGainAdjustmentChanged = [this](int orderInMix, float newGain)
                    {
                        if (m_isReadOnly)
                        {
                            spdlog::info("Cannot modify gain - mix is read-only");
                            return;
                        }
                        
                        // Find the MixTrack in the loader's vector
                        MixTrack* targetMixTrack = nullptr;
                        // m_mixLoader is checked against nullptr at the beginning of the function, so false PVS Studio positive
                        auto& mixTracks = m_mixLoader->getMixTracks(); // Get mutable reference to the vector  //-V1053 // -V595
                        for (auto& mt : mixTracks) {
                            if (mt.orderInMix == orderInMix) {
                                targetMixTrack = &mt;
                                break;
                            }
                        }

                        if (targetMixTrack) {
                            spdlog::debug("TimelineComponent: updating gain for track order {} to {}", orderInMix, newGain);

                            // Create a copy of the found MixTrack to pass to the manager
                            MixTrack updatedTrack = *targetMixTrack;
                            updatedTrack.gainAdjustment = newGain;

                            // Call the new updateMixTrack method on the MixManager
                            if (m_mixLoader && m_mixLoader->getMixId() > 0) {
                                if (!theTrackLibrary.getMixManager().updateMixTrack(m_mixLoader->getMixId(), updatedTrack)) {
                                    spdlog::error("Failed to update single MixTrack gain for mix {} track {}", m_mixLoader->getMixId(), updatedTrack.trackId);
                                } else {
                                    // If successful, update the in-memory MixTrack in the loader as well
                                    // This is crucial to keep the UI and internal model in sync
                                    targetMixTrack->gainAdjustment = newGain;

                                    // Reload mix in playback controller so the audio thread picks up the new gain
                                    if (onMixPlaybackReloadRequested)
                                    {
                                        onMixPlaybackReloadRequested();
                                    }
                                    else
                                    {
                                        spdlog::error("TimelineComponent: onMixPlaybackReloadRequested is null; gain change won't reach the audio engine");
                                    }
                                }
                            }
                        } else {
                            spdlog::error("MixTrack with orderInMix {} not found in loader for gain adjustment.", orderInMix);
                        }

                        // No need to call onMixChanged here, as updateMixTrack handles its own undo and persistence
                        // The UI will be refreshed by the TimelineComponent's own update mechanism if needed.
                    };
                    view.component->onShowTrackInLibrary = [this](TrackId trackId)
                    {
                        if (onShowTrackInLibraryRequested)
                            onShowTrackInLibraryRequested(trackId);
                    };
                    view.component->onShowTrackDetails = [this](TrackId trackId)
                    {
                        if (onShowTrackDetailsRequested)
                            onShowTrackDetailsRequested(trackId);
                    };
                    view.component->onCueDragInProgress = [this](int orderInMix, bool isAttachPoint, std::optional<Duration_t> previewTime)
                    {
                        if (previewTime.has_value())
                        {
                            // Find the track view for this orderInMix
                            for (const auto &tv : m_trackViews)
                            {
                                if (tv.mixTrackData && tv.mixTrackData->orderInMix == orderInMix)
                                {
                                    // xToTime returns cueStart + offset_within_component
                                    // componentStartTime is where the component starts on the timeline
                                    // So absolute position = componentStartTime + offset_within_component
                                    
                                    // Debug for track 22650
                                    if (tv.mixTrackData->trackId == 22650)
                                    {
                                        spdlog::info("[Track 22650 Timeline Debug] Preview line calculation:");
                                        spdlog::info("  - componentStartTime: {} ms", tv.componentStartTime.count());
                                        spdlog::info("  - previewTime: {} ms", previewTime->count());
                                        spdlog::info("  - cueStart: {} ms", tv.mixTrackData->cueStart.count());
                                        spdlog::info("  - offset: {} ms", (*previewTime - tv.mixTrackData->cueStart).count());
                                        spdlog::info("  - final preview position: {} ms", 
                                            (tv.componentStartTime + (*previewTime - tv.mixTrackData->cueStart)).count());
                                    }
                                    
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
            if (auto *viewport = findParentComponentOfClass<juce::Viewport>()) // JUCE_API
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

            m_isPopulating = false; // Reset flag at the end
            return true;
        }

        void TimelineComponent::drawCrossfadeLines(juce::Graphics &g)
        {
            // OPTIMIZATION: Only draw crossfade lines that are visible
            auto clipBounds = g.getClipBounds();
            const float clipLeft = static_cast<float>(clipBounds.getX());
            const float clipRight = static_cast<float>(clipBounds.getRight());
            
            // For each consecutive pair of tracks, draw the attach/crossfade region
            for (size_t i = 0; i < m_trackViews.size(); ++i)
            {
                if (i + 1 >= m_trackViews.size())
                    break; // No next track to crossfade with

                const auto &currentView = m_trackViews[i];
                const auto &currentTrack = *currentView.mixTrackData;

                // The ATTACH point is calculated relative to the AUDIO start time, not the component start time.
                const double currentAudioStart = std::chrono::duration<double>(currentView.audioStartTime).count();
                const double attachToTime = currentAudioStart + std::chrono::duration<double>(currentTrack.attachTo).count();

                const float attachX = static_cast<float>(attachToTime * m_pixelsPerSecond);
                
                // Only draw if the line is visible
                if (attachX >= clipLeft - 1 && attachX <= clipRight + 1)
                {
                    g.setColour(juce::Colours::orange.withAlpha(0.7f));
                    g.drawVerticalLine(juce::roundToInt(attachX), 0.0f, static_cast<float>(getHeight()));
                    
                    g.setFont(10.0f);
                    g.setColour(juce::Colours::orange);
                    g.drawText("ATTACH", juce::roundToInt(attachX) - 20, 5, 40, 12, juce::Justification::centred);
                }
            }
        }
    } // namespace ui
} // namespace jucyaudio
