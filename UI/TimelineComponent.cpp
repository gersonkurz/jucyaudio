#include <UI/MixEditorComponent.h>
#include <UI/Settings.h>
#include <UI/TimelineComponent.h>
#include <UI/PlaybackController.h>
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
                    // This now calls the corrected method below
                    deleteSelectedTrack();
                    return true; 
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

            // 2. Get the ID of the track to delete
            const auto trackIdToRemove{m_selectedTrack->getTrackId()};
            const auto currentMixId{m_mixLoader->getMixId()};
            spdlog::info("Attempting to delete Track ID: {} from Mix ID: {}", trackIdToRemove, currentMixId);

            // 3. Check if we need to show a confirmation dialog (logic is unchanged)
            bool shouldRemoveFromWorkingSet = false;
            bool userCancelled = false;

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
                        if (config::theSettings.mixEditingSettings.askBeforeRemovingFromWorkingSet.get())
                        {
                            const auto result = juce::AlertWindow::showYesNoCancelBox(juce::AlertWindow::QuestionIcon,
                                "Remove Track",
                                "Remove this track from the mix?\n\nAlso remove from the source working set?",
                                "Remove from Both",
                                "Remove from Mix Only",
                                "Cancel");

                            if (result == 0) userCancelled = true;
                            else if (result == 1) shouldRemoveFromWorkingSet = true;
                        }
                        else
                        {
                            shouldRemoveFromWorkingSet = true;
                        }
                    }
                }
            }

            if (userCancelled)
            {
                spdlog::info("User cancelled track removal");
                return false;
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

            // 4. Perform the database deletion from the mix
            if (!theTrackLibrary.getMixManager().removeTrackFromMix(currentMixId, trackIdToRemove))
            {
                spdlog::error("Failed to remove track from database.");
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

            // 6. CRITICAL: Refresh the in-memory loader from the database.
            if (!m_mixLoader->reloadFromDatabase())
            {
                spdlog::error("Failed to reload MixProjectLoader from database after deletion!");
                return false;
            }

            // --- FIX: Reload the now-modified mix into the playback engine ---
            spdlog::debug("TimelineComponent: Reloading mix in playback controller.");
            bool loadSuccess = playbackController->loadMix(m_mixLoader);

            // --- FIX: Resume playback if it was active before ---
            if (wasPlaying && loadSuccess)
            {
                spdlog::debug("TimelineComponent: Resuming playback from position {}", playbackPosition);
                playbackController->playMixFrom(playbackPosition);
            }

            // 7. Repopulate the UI from the fresh, updated loader.
            return populateFrom();
        }

        void TimelineComponent::copySelectedTrackToClipboard()
        {
            if (!m_selectedTrack || !m_mixLoader)
            {
                spdlog::warn("copySelectedTrackToClipboard: No track selected or no mix loaded");
                return;
            }

            const auto trackId = m_selectedTrack->getTrackId();

            // Find the track data in our views
            for (const auto &view : m_trackViews)
            {
                if (view.mixTrackData && view.trackInfoData && view.mixTrackData->trackId == trackId)
                {
                    // Copy the data to clipboard
                    m_clipboard.mixTrack = *view.mixTrackData;
                    m_clipboard.trackInfo = *view.trackInfoData;
                    m_clipboard.isValid = true;

                    spdlog::info("Copied track {} to clipboard", trackId);
                    return;
                }
            }

            spdlog::error("Failed to find track data for clipboard copy");
        }

        void TimelineComponent::cutSelectedTrackToClipboard()
        {
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
                if (removeTrackFromMixOnly(trackId))
                {
                    spdlog::info("Cut track {} to clipboard", trackId);
                }
            }
        }

        void TimelineComponent::pasteFromClipboard(bool insertBefore)
        {
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
                const auto &nextTrack = mixTracks[0];
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
            const auto currentMixId = m_mixLoader->getMixId();

            if (theTrackLibrary.getMixManager().createOrUpdateMix(mixInfo, mixTracks))
            {
                spdlog::info("Pasted track {} from clipboard at position {}", mixTrackToInsert.trackId, insertIndex);

                // Reload from database to get the updated mix
                if (m_mixLoader->reloadFromDatabase())
                {
                    // Refresh the UI
                    populateFrom();

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

            // Collect IDs of tracks to remove (all with orderInMix > selectedOrder)
            std::vector<TrackId> tracksToRemove;
            for (const auto &track : mixTracks)
            {
                if (track.orderInMix > selectedOrder)
                {
                    tracksToRemove.push_back(track.trackId);
                }
            }

            if (tracksToRemove.empty())
            {
                spdlog::info("removeAllTracksAfterSelected: No tracks to remove after selected track");
                return;
            }

            spdlog::info("Removing {} tracks after order {} (from mix only, keeping in working set)", tracksToRemove.size(), selectedOrder);

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

            const auto currentMixId = m_mixLoader->getMixId();
            
            // Use the efficient batch removal method
            if (theTrackLibrary.getMixManager().removeTracksFromMix(currentMixId, tracksToRemove))
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

                    if (onMixChanged)
                    {
                        onMixChanged();
                    }

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
                spdlog::error("Failed to remove tracks from mix using batch operation.");
            }
        }

        bool TimelineComponent::removeTrackFromMixOnly(TrackId trackIdToRemove)
        {
            if (!m_mixLoader)
            {
                spdlog::error("removeTrackFromMixOnly called but m_mixLoader is null.");
                return false;
            }

            const auto currentMixId = m_mixLoader->getMixId();
            spdlog::info("Removing track {} from mix {} (mix only, not working set)", trackIdToRemove, currentMixId);

            // Remove from mix database
            if (!theTrackLibrary.getMixManager().removeTrackFromMix(currentMixId, trackIdToRemove))
            {
                spdlog::error("Failed to remove track from mix.");
                return false;
            }

            // Clear selection if we removed the selected track
            if (m_selectedTrack && m_selectedTrack->getTrackId() == trackIdToRemove)
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

            if (onMixChanged)
            {
                onMixChanged();
            }

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
            const auto trackId = mixTracks[trackIndex].trackId;
            const auto currentMixId = m_mixLoader->getMixId();

            if (theTrackLibrary.getMixManager().removeTrackFromMix(currentMixId, trackId))
            {
                spdlog::info("Deleted track at index {}", trackIndex);

                // Clear selection if we deleted the selected track
                if (m_selectedTrack && m_selectedTrack->getTrackId() == trackId)
                {
                    m_selectedTrack = nullptr;
                }

                // Reload from database to get the updated mix
                if (m_mixLoader->reloadFromDatabase())
                {
                    // Refresh the UI
                    populateFrom();

                    if (onMixChanged)
                    {
                        onMixChanged();
                    }
                }
                else
                {
                    spdlog::error("Failed to reload mix after deleting track");
                }
            }
            else
            {
                spdlog::error("Failed to delete track at index {}", trackIndex);
            }
        }

        void TimelineComponent::refreshAfterDeletion(TrackId deletedTrackId)
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
                [deletedTrackId](const TrackView &view)
                {
                    return view.component && view.component->getTrackId() == deletedTrackId;
                });

            if (it != m_trackViews.end())
            {
                if (m_selectedTrack == it->component.get())
                {
                    m_selectedTrack = nullptr;
                }
                m_trackViews.erase(it);
                spdlog::info("Removed TrackView for track ID {}", deletedTrackId);
            }
            else
            {
                spdlog::warn("Could not find TrackView for deleted track ID {}. Performing full repopulation.", deletedTrackId);
                populateFrom();
                return;
            }

            // Step 2: Update data pointers and re-sort the view vector
            const auto &newMixTracks = m_mixLoader->getMixTracks();
            std::unordered_map<TrackId, database::MixTrack *> newDataMap;
            for (const auto &track : newMixTracks)
            {
                newDataMap[track.trackId] = const_cast<database::MixTrack *>(&track);
            }

            for (auto &view : m_trackViews)
            {
                auto findIt = newDataMap.find(view.component->getTrackId());
                if (findIt != newDataMap.end())
                {
                    view.mixTrackData = findIt->second;
                }
                else
                {
                    spdlog::error("Inconsistent state in refreshAfterDeletion for track {}. Falling back to full populate.", view.component->getTrackId());
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
                const int seconds = (i * 30) % 60;
                const int hours = minutes / 60;
                minutes %= 60;
                juce::String time = juce::String::formatted("%d:%02d:%02d", hours, minutes, seconds);
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
                float dashLengths[] = {4.0f, 4.0f};
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
                auto &view = m_trackViews[trackIndex];
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

                previousAudioStartTime = view.audioStartTime;
            }

            // Refresh the layout with the new positions
            refreshLayout();
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

                    view.component->onCueAttachChanged = [this](int orderInMix, const database::MixTrack &updatedTrack)
                    {
                        if (!m_isPopulating && onCueAttachChanged)
                            onCueAttachChanged(orderInMix, updatedTrack);
                    };
                    view.component->onEnvelopeChanged = [this](int orderInMix, const std::vector<database::EnvelopePoint> &points)
                    {
                        if (!m_isPopulating && onEnvelopeChanged)
                            onEnvelopeChanged(orderInMix, points);
                    };
                    view.component->onGainAdjustmentChanged = [this](int orderInMix, float newGain)
                    {
                        // Find the MixTrack in the loader's vector
                        MixTrack* targetMixTrack = nullptr;
                        auto& mixTracks = m_mixLoader->getMixTracks(); // Get mutable reference to the vector
                        for (auto& mt : mixTracks) {
                            if (mt.orderInMix == orderInMix) {
                                targetMixTrack = &mt;
                                break;
                            }
                        }

                        if (targetMixTrack) {
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
                                }
                            }
                        } else {
                            spdlog::error("MixTrack with orderInMix {} not found in loader for gain adjustment.", orderInMix);
                        }
                        
                        // No need to call onMixChanged here, as updateMixTrack handles its own undo and persistence
                        // The UI will be refreshed by the TimelineComponent's own update mechanism if needed.
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
