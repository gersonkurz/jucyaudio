# JucyAudio - Open Tasks

## Scheduled Exports

When you click on Export mix, and select "Schedule for Later" - the mix tracks aren't removed. So when you create ANOTHER mix, it will include the tracks you had selected for this mix. 
And it's probably not a good idea to remove them from the second mix, - not sure, but probably this action affects the "scheduled" mix as well...

---

## Nice-to-have: Show mix progress / track counts in mix editor

**Context**: When working with a large mix, there's no indication of how many tracks remain to be mixed vs. how many are already in the mixed area. Makes it hard to gauge progress.

**Fix**: Display something like "15/42 tracks mixed" or "27 remaining" in the mix editor UI — showing both the mixed and unmixed counts.

---

## Bug: Gain adjustment knob resets playback position

**Symptom**: Dragging the per-track gain knob (upper right of each waveform component) causes playback to jump back to the beginning of that track, even though the playhead was somewhere else in the mix.

**Likely cause**: The gain change triggers a mix reload or playback controller re-load that resets the playback position instead of applying the gain change in-place.

**Key files to investigate**:
- `UI/MixTrackComponent.cpp` — the gain knob callback (look for `gainAdjustment` or slider/knob changed handler)
- `UI/MixEditorComponent.cpp` — how gain changes propagate to the playback controller
- `Audio/MixProjectLoader.cpp` — whether a reload is triggered

**Fix approach**: The gain change should update the in-memory `MixTrack::gainAdjustment`, persist to DB, and notify the audio engine without reloading the mix or resetting the playback position.

---

## Bug: Track editor shows wrong delete dialog for mix tracks

**Symptom**: In the Track Editor view of a mix, pressing delete on a track shows the "Delete from library" / "Delete from library and filesystem" confirmation dialog. This is wrong — the user just wants to remove the track from the mix (and its source working set), not delete it from the library entirely.

**Fix approach**: The Track Editor's delete handler needs to check its context. When viewing a mix's tracks, it should behave like the Mix Editor's remove-track action: remove from mix + optionally from the source working set. The library-delete dialog should only appear when browsing the actual library/folder views.

---

## Performance: Track editor vertical scrolling is very slow

**Symptom**: In the Track Editor view (as opposed to the Mix Editor), scrolling up/down through the track list is noticeably slow/laggy.

**Key files to investigate**:
- The Track Editor component and its list/table view
- Check if rows are being re-rendered or re-queried on every scroll event
- Check if waveform rendering or heavy per-row operations are happening during scroll

---

## Performance: Resizing the main window is extremely slow with a large mix open

**Symptom**: Dragging the window width takes ~30 seconds to respond when a large mix is loaded in the mix editor. The resize should not trigger any mix-related recalculations.

**Important**: This happens even with very few tracks — it's not proportional to track count. The bottleneck is something fundamental in the resize chain, not the per-track layout cost.

**Likely cause**: Something expensive is being triggered on every resize frame — possibly waveform re-rendering, viewport recalculation, or a cascading `setSize()` / `resized()` loop. Could also be the split pane / layout manager doing redundant work.

**Fix approach**:
- Profile the resize path to find the actual bottleneck (add timing to key `resized()` methods)
- Defer/throttle expensive operations during continuous resize
- Check for any resize feedback loops (setSize triggering resized triggering setSize)

**Key files**:
- `UI/TimelineComponent.cpp` — `resized()` (line 1340+), `refreshLayout()`
- `UI/MixEditorComponent.cpp` — how resize propagates
- `UI/MainComponent.cpp` — top-level resize handling

---

## Feature: Show track details from mix editor

**Symptom**: In the mix editor, there's no way to see a track's filename, path, format, bitrate, or other file properties. The waveform just shows artist/title but nothing else.

**Recommended approach**: Right-click context menu action "Show in Library" that navigates to and highlights the track in the library tree. This gives access to all existing track details without duplicating UI. Additionally, a properties dialog (or the existing track details view) accessible from the mix editor's context menu would cover the "quick glance" case without leaving the mix.

---

## Bug: Waveform loading failure message is useless

**Symptom**: When creating a mix and one waveform fails to load, the final dialog shows: `"Loading 0 of 1 waveforms" (1 failed, 276 cached) - 0%`. This tells you nothing useful — not which track failed, nor why.

**Fix approach**:
- Collect failed track names/IDs during the waveform loading task
- Show a meaningful completion message like: "1 waveform failed to load: Artist - Title.mp3 (file not found)" or at minimum list the filenames that failed
- The progress message format itself is also confusing ("Loading 0 of 1") — should say something like "Failed to load 1 waveform"

---

## Bug: Drag-and-drop reordering of tracks in mix editor doesn't work

**Symptom**: You can pick up a track and see it visually moving as you drag, but there's no way to actually drop it into a new position before/after another track. The drop never registers — the track just snaps back to its original position.

**Likely cause**: The drop target / hit detection logic isn't recognizing valid drop zones between tracks, or the `itemDropped` / `isInterestedInDragSource` callbacks aren't wired up correctly on the receiving end.

**Key files to investigate**:
- `UI/TimelineComponent.cpp` — drag-and-drop handling
- `UI/MixTrackComponent.cpp` — drag source setup
- `UI/MixEditorComponent.cpp` — any drop zone logic

---

## Bug: Mix editor loses scroll/viewport position when navigating away and back

**Symptom**: When you leave the mix editor (e.g. to browse the library) and return, the horizontal scroll position resets to the beginning. You lose your place and have to manually scroll back to where you were editing.

**Fix approach**: Save the viewport's scroll position (and ideally the zoom level) when navigating away from a mix, and restore it when returning. This could be stored on the MixNode or in the MixEditorComponent as transient state — no need to persist to DB.

---

## Visual: Mix editor horizontal scrollbar thumb is too small

**Symptom**: The horizontal scrollbar at the bottom of the mix editor has a very small drag thumb, making it hard to grab — especially for long mixes where the thumb shrinks further.

**Fix approach**: Override the scrollbar's minimum thumb size in the LookAndFeel or set a minimum thumb width so it remains easily grabbable regardless of mix length. JUCE's `LookAndFeel::getMinimumScrollbarThumbSize()` or custom scrollbar styling should handle this.

---


## Nice-to-have: Populate the empty status bar

**Context**: The main UI has a status bar at the bottom that is currently always empty. It should show contextual info like the number of tracks currently displayed, total duration, current view/filter state, etc.

---

## Nice-to-have: Dual progress bars for batch export

**Context**: Batch export currently shows a single progress bar with "Exporting 1/2: MixName...". Since each mix export takes a long time, it's hard to gauge progress — the bar jumps in large steps.

**Fix**: Show two progress indicators — one for overall batch progress (e.g. "Mix 2 of 7") and one for the current mix's export progress. May require a small extension to `TaskDialog` to support a second progress bar, or a custom batch export progress dialog.

---

## Low priority: Handle corrupted/truncated audio tracks gracefully

**Symptom**: Some tracks render as a flat line (no waveform) or start rendering normally then cut off to silence — indicating the audio file is corrupted or truncated.

**Fix approach (two parts)**:
- a) **Prune to valid data**: Detect the actual valid audio length during waveform analysis or mix creation. Set the track's effective duration to match only the valid portion so attach points and envelope don't extend into dead data.
- b) **Exclude fully broken tracks**: If a track has zero valid audio data, it should not be added to the mix at all (or flagged/auto-removed with a warning). The existing `TrackStatus::BadFormat` mechanism could be leveraged here.

**Complexity**: This touches the audio decode pipeline, waveform analysis, and mix creation logic. Needs careful handling to avoid false positives on tracks that are legitimately silent at the end.

---
