# JucyAudio - Open Tasks

If you are exporting from a Workingset named FOO, and you want to export, default to a folder named FOO, not "the first one in the list".

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

---

## Deferred: `SqliteFolderDatabase` lock-order inversion

**Symptom**: Two threads can deadlock. The cache accessors (`getFolderById`, `hasChildren`,
`getParentSet`, `getChildFolders`, `getAllChildFolders`) call `buildCacheIfNeeded()`, which takes
`m_cacheMutex` (`Database/Sqlite/SqliteFolderDatabase.cpp:22`) and then acquires the database mutex
through `SqliteStatement`. `findOrCreateFolderByPath` (`:660`) takes the database mutex first and then
wants `m_cacheMutex`.

**Why it has not bitten yet**: `buildCacheIfNeeded` only reaches for the database mutex when the cache
is invalid. That happens during scans — which is exactly when `findOrCreateFolderByPath` runs hardest,
so the window is narrow rather than absent.

**Fix approach**: one consistent order, database mutex before cache mutex, on both paths.

---

## Deferred: deferred transactions are not isolated against the same connection

**Symptom**: `TransactionMode::Immediate` (added for the mix recovery capture) holds
`SqliteDatabase::getMutex()` for the transaction's lifetime, so nothing else on the connection can
interleave. `TransactionMode::Deferred` — the default, used everywhere else — does not. Another thread
on the same connection can write inside someone else's deferred transaction.

**Key files**: `Database/Sqlite/SqliteTransaction.{h,cpp}`, `Database/Sqlite/SqliteDatabase.cpp`.

**Fix approach**: not simply switching the default. Immediate mode serialises the whole connection for
the transaction's duration, which is correct for a short capture and would be a throughput problem for
a scan. Each deferred call site needs deciding on its own.

---

## Deferred: force-rescan re-insert collision

**Symptom**: A forced rescan re-inserts a track row that already exists rather than updating the
columns the scanner owns, so it collides.

**Fix approach**: a targeted "update only scanner-owned columns" operation. The same operation is what
scanner-driven re-identification (matching a moved file back to its `MixRecovery` entry by filename and
size) needs, so the two should be done together.

**Key files**: `Database/TrackScanner.cpp`, `Database/Sqlite/SqliteTrackDatabase.cpp`.

---

## Deferred: repair the 98 mixes damaged by the old `MixTracks` cascade

**Symptom**: 98 mixes have fewer `MixTracks` rows than their `track_count`, with gaps in
`order_in_mix`. Caused by `MixTracks.track_id` having no foreign key while deleting a track removed the
row anyway; `removeTracksFromMix` renumbers, so a gap is the cascade's signature rather than an edit's.

These are the mixes `--export-mix-recovery` refuses to record, by design — an incomplete mix cannot be
described honestly. The full list is in `%LOCALAPPDATA%\jucyaudio\backfill-results.txt` after a run.

**Fix approach**: unknown until someone checks whether an older snapshot in
`%LOCALAPPDATA%\jucyaudio\*.sqlite` (23-2026-08-05 through 27-2026-08-28) still holds the missing
`MixTracks` rows. One read-only query settles it. Recording them is blocked until they are repaired.

---

## Deferred: three different ATTACH walks disagree about an unresolvable track

**Symptom**: `calculateMixDuration` (`Database/Includes/MixInfo.h`), `MixPlaybackEngine` and
`ExportMixImplementation` each handle a mix row whose track cannot be resolved differently. The walk
and the exporter skip the row but still use its `attachTo` to position the next track; the playback
engine advances through every row. With such a row present, the three would place tracks at different
positions and report different totals.

**Why it has not bitten**: it cannot happen today. `MixTracks.track_id` is a foreign key with
`ON DELETE CASCADE`, so deleting a track removes its mix row rather than leaving it dangling, and zero
rows in the library point at a missing track. `calculateMixDuration` deliberately copies the
exporter's rule so that a stored length cannot disagree with the audio the exporter renders.

**Fix approach**: one walk, used by all three. The blocker is that the playback engine and the
exporter each own their own positioning loop for reasons unrelated to this - unpicking those is the
work, not choosing the rule.

---

## Deferred: the timeline holds raw pointers into a vector that gets replaced

**Symptom**: `TrackView::mixTrackData` points into `MixProjectLoader::m_mixTracks`
(`UI/TimelineComponent.cpp:1664`). `MixProjectLoader::loadMix` clears and refills that vector, so
every reload invalidates every TrackView pointer at once. Painting, layout, dragging or copying a
stale TrackView reads freed memory.

**Why it has not bitten more often**: reloads normally arrive through the editor, which repopulates
the timeline immediately afterwards. The metadata refresh in `MainComponent` did not, and now calls
`MixEditorComponent::onNodeCacheReloaded` to do so - but that is one caller being taught the rule,
not the rule being enforced.

**Fix approach**: the timeline should not retain pointers into replaceable storage. Indices into the
loader, or a copy it owns, remove the whole class of problem rather than requiring every future
refresh site to remember. Until then, anything calling `refreshCache(true)` on a node the editor may
be showing has to notify the editor.

---

## Deferred: the mix editor cannot tell a failed track query from an offline one

**Symptom**: `MixProjectLoader::loadMix` reads its `TrackInfo` list with
`theTrackLibrary.getTracks(...)`, which returns an empty vector both when the query fails and when it
legitimately matches nothing. The loader then reports success either way, and the timeline and
playback see a mix whose tracks do not resolve.

**Why the obvious check is wrong**: an empty result is not evidence of failure. The ordinary track
query filters out offline folders, so a valid mix whose tracks are all on a disconnected drive
resolves to nothing - and rejecting that would make those mixes unopenable whenever the volume is
unplugged. A partial failure does not show up as emptiness either; it comes back as a prefix.

**Fix approach**: a status-bearing read that fetches the TrackInfos for a specific set of track ids,
without the offline filter, and reports whether the query itself succeeded. Until then an
unresolvable track is skipped by the ATTACH walk and by the timeline, as it always has been.
