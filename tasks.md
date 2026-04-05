# JucyAudio - Open Tasks

Select n tracks. Click "Remove" in the context menu. They are removed, but the selection remains.
If you are exporting from a Workingset named FOO, and you want to export, default to a folder named FOO, not "the first one in the list".

---

## Feature: Show track details from mix editor

**Symptom**: In the mix editor, there's no way to see a track's filename, path, format, bitrate, or other file properties. The waveform just shows artist/title but nothing else.

**Recommended approach**: Right-click context menu action "Show in Library" that navigates to and highlights the track in the library tree. This gives access to all existing track details without duplicating UI. Additionally, a properties dialog (or the existing track details view) accessible from the mix editor's context menu would cover the "quick glance" case without leaving the mix.

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

## Low priority: Handle corrupted/truncated audio tracks gracefully

**Symptom**: Some tracks render as a flat line (no waveform) or start rendering normally then cut off to silence — indicating the audio file is corrupted or truncated.

**Fix approach (two parts)**:
- a) **Prune to valid data**: Detect the actual valid audio length during waveform analysis or mix creation. Set the track's effective duration to match only the valid portion so attach points and envelope don't extend into dead data.
- b) **Exclude fully broken tracks**: If a track has zero valid audio data, it should not be added to the mix at all (or flagged/auto-removed with a warning). The existing `TrackStatus::BadFormat` mechanism could be leveraged here.

**Complexity**: This touches the audio decode pipeline, waveform analysis, and mix creation logic. Needs careful handling to avoid false positives on tracks that are legitimately silent at the end.

---
