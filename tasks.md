# JucyAudio - Open Tasks

## Bug: Mix editor loses scroll/viewport position when navigating away and back

**Symptom**: When you leave the mix editor (e.g. to browse the library) and return, the horizontal scroll position resets to the beginning. You lose your place and have to manually scroll back to where you were editing.

**Fix approach**: Save the viewport's scroll position (and ideally the zoom level) when navigating away from a mix, and restore it when returning. This could be stored on the MixNode or in the MixEditorComponent as transient state — no need to persist to DB.

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

## Deferred: 95 mixes lost tracks, and those tracks are gone

**What happened**: `MixTracks.track_id` used to have no foreign key while deleting a track removed
the row anyway, leaving gaps in `order_in_mix`. 95 mixes are affected.

**What was checked**: every surviving snapshot in `%LOCALAPPDATA%\jucyaudio\*.sqlite` was compared
against the live library on 2026-08-30. None of them holds a single one of the missing rows - the
damage predates 2026-08-15, and the two older snapshots have since been pruned. The tracks are not
recoverable.

**What was done instead**: `just backup-damaged-mixes` records what survived of them, marked partial,
so they stop being the only mixes in the library with nothing written down. A partial record never
replaces a whole one, and is never written beside a rendered export.

**Note on detection**: the duration repair set `track_count` to the number of surviving rows, so
"expected 74, found 72" no longer identifies these. Gaps in `order_in_mix`, and `is_complete = 0` in
`MixRecovery`, are what remain.

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
