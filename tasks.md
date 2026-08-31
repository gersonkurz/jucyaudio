# JucyAudio - Open Tasks

Ordered by priority: **P1** (fix before tagging 2.0) → **P3** (whenever). Nothing is P1 right now:
the memory-safety and deadlock items are done. P2 items are reachable correctness or user-visible
defects that are not memory-unsafe; P3 items cannot happen today, are bounded to a logged
stale-state effect, or need a design decision first.

---

## P2: a failed read in `removeEmptyFolders` reads as an empty library

**Symptom**: `removeEmptyFolders` builds its set of folders that are in use by iterating
`SELECT DISTINCT folder_id FROM Tracks` (`Database/Sqlite/SqliteFolderDatabase.cpp:628`) and never
checks `selectStmt.hasError()` afterwards. A query that fails, or stops partway, is indistinguishable
from one that found nothing - and every folder missing from that set is then deleted. `Folders` carries
`ON DELETE CASCADE` on `parent_id`, and the `Tracks` rows go with the folders, so a failed read here
deletes library content rather than declining to.

**Why it has not bitten**: the read is a plain scan of one column on a healthy connection. The
successful path is exercised by the folder cache self test; the failure path is not reachable from a
test without breaking the connection under it.

**Fix approach**: `hasError()` after the loop, and refuse the whole operation if it is set - the
transaction is already there to roll back. `SqliteMixSummary.cpp:75,111` is the pattern to follow.
Pre-dates the lock-order work and was found while reviewing it.

---

## P2: nothing stops a folder path from being stored twice

**Symptom**: `Folders` has no unique index on its path column - only
`idx_folders_parent_name ON Folders(parent_id, name)`, which is not unique
(`Database/Sqlite/SqliteTrackDatabase.cpp:183`). A second row for a path that already has one is
therefore a legal insert, and the consequence does not stay small: `buildCacheIfNeeded` refuses to
finish a cache that holds two rows for one path
(`Database/Sqlite/SqliteFolderDatabase.cpp:141`, `return false`), so from the first duplicate onwards
the cache can never be rebuilt, every lookup misses, and every folder touched after that gets another
row of its own.

**How it was found**: the folder cache self test produced exactly that cascade - one duplicate, then
1505 failed cache builds in a single run - against an `invalidateCache()` that did not hold the
database mutex. That hole is closed, so nothing reachable today inserts a duplicate. What remains is
that the schema does not say it cannot happen, and the failure mode if it ever does is silent and
permanent.

**Fix approach**: a unique index on the path column, and a decision about what an insert that violates
it should do - `findOrCreateFolderByPath` currently treats a failed insert as "could not create" and
returns -1, which is right for a caller but says nothing about the row that already exists. Needs the
three-place schema change (`initialSqlStatements`, `latestSchemaVersion`, `runMigrations`) and a
migration that copes with a database that already holds duplicates.

---

## P2: three different ATTACH walks disagree about an unresolvable track

**Symptom**: `calculateMixDuration` (`Database/Includes/MixInfo.h:315`), `MixPlaybackEngine`
(`Audio/MixPlaybackEngine.cpp:49`) and `ExportMixImplementation::calculateTrackPositions`
(`Audio/ExportMixImplementation.cpp:112`) each handle a mix row whose track cannot be resolved
differently. The walk and the exporter skip the row without advancing `previousTrackStart`; the
playback engine advances through every row. With such a row present, the three place tracks at
different positions and report different totals.

**It is reachable today**, which this entry used to deny: an unresolvable row does not require a
dangling foreign key. `showOfflineTracks` defaults to false (`UI/Settings.h:166`), the ordinary track
query then excludes offline folders (`Database/Sqlite/SqliteStatementConstruction.cpp:94`), and
`MixProjectLoader::loadMix` reads its TrackInfos with that query (`Audio/MixProjectLoader.cpp:72`).
So an intact mix with one track on a disconnected drive resolves to a `MixTracks` row with no
TrackInfo, and the three walks disagree about it. The stale "no mix row points at a track that cannot
be resolved" claim is also in the comment above `calculateMixDuration` and should go with it.

**Fix approach**: one walk, used by all three. The blocker is that the playback engine and the
exporter each own their own positioning loop for reasons unrelated to this - unpicking those is the
work, not choosing the rule. Related: the statusless read below is what makes an offline track
indistinguishable from a failed query in the first place.

---

## P2: the mix editor cannot tell a failed track query from an offline one

**Symptom**: `MixProjectLoader::loadMix` reads its `TrackInfo` list with
`theTrackLibrary.getTracks(...)` (`Audio/MixProjectLoader.cpp:72`), which returns an empty vector both
when the query fails and when it legitimately matches nothing. The loader publishes that result and
sets `m_loaded = true` either way (`:97`), so the timeline and playback see a mix whose tracks do not
resolve, and the read-only guard in the editor does not catch it: that fires on
`!node->isCacheLoaded()` (`UI/MixEditorComponent.cpp:542`), which covers a failed `readMixTracks`, not
this query. An unresolvable track is simply skipped by the ATTACH walk and by the timeline, as it
always has been.

**Why the obvious check is wrong**: an empty result is not evidence of failure. The ordinary track
query filters out offline folders, so a valid mix whose tracks are all on a disconnected drive
resolves to nothing - and rejecting that would make those mixes unopenable whenever the volume is
unplugged. A partial failure does not show up as emptiness either; it comes back as a prefix.

**Fix approach**: a status-bearing read that fetches the TrackInfos for a specific set of track ids,
without the offline filter, and reports whether the query itself succeeded.

---

## P2: deferred transactions are not isolated against the same connection

**Symptom**: `TransactionMode::Immediate` (added for the mix recovery capture) holds
`SqliteDatabase::getMutex()` for the transaction's lifetime, so nothing else on the connection can
interleave. `TransactionMode::Deferred` — the default, used everywhere else — does not. Another thread
on the same connection can write inside someone else's deferred transaction.

**Key files**: `Database/Sqlite/SqliteTransaction.{h,cpp}`, `Database/Sqlite/SqliteDatabase.cpp`.

**Fix approach**: not simply switching the default. Immediate mode serialises the whole connection for
the transaction's duration, which is correct for a short capture and would be a throughput problem for
a scan. Each deferred call site needs deciding on its own.

---

## P3: a folder read can miss while the cache is being rebuilt

**Symptom**: the cache accessors (`getFolderById`, `hasChildren`, `getParentSet`, `getChildFolders`,
`getAllChildFolders`) call `buildCacheIfNeeded()`, which returns having released both mutexes, and then
take `m_cacheMutex` for the read itself (`Database/Sqlite/SqliteFolderDatabase.cpp:449` onwards). An
`invalidateCache()` landing in that gap empties the maps, and the read reports the folder as absent -
a folder that momentarily has no children, or no name, in the middle of navigation.

**Why it is not worse**: nothing is written from those paths, so the miss is transient and the next
access rebuilds the cache. The lock order is now consistent, so the same gap can no longer produce a
duplicate folder row - that was the same window and it is closed.

**Fix approach**: hold both mutexes across the build and the read, in the established order. That makes
every accessor wait for the database mutex, which is what the fast path in `buildCacheIfNeeded`
deliberately avoids, so the useful shape is probably to keep the fast path and take both only on the
build-and-read path. Measured on the folder cache self test, with a thread doing nothing but
invalidating: 0 of 300 reads hit it.

---

## P3: a stale timeline is corrected on the next edit, not before

**Symptom**: a reload that the timeline was not told about (the mix rows path in
`UI/DataViewComponent.cpp:800`, for one) leaves it showing the rows it was built from. Editing that
picture is refused now - every write path calls `refuseIfViewsAreStale`
(`UI/TimelineComponent.cpp:1059`), which compares `MixProjectLoader::getContentsGeneration()` against
the generation stamped at populate time, logs, and schedules a repopulation - so nothing writes to a
row it did not mean. What is left is the display: until the user tries to edit, the timeline shows the
previous contents and nothing corrects it.

**Fix approach**: notify rather than detect. The loader could tell whoever is showing its rows that
they changed, which is the same information `getContentsGeneration` exposes, pushed instead of polled.
Not urgent while the only consequence is a stale picture that the next interaction fixes.

---

## P3: re-identify a returned file against `MixRecovery`

**What is left**: a scan matches a returned file back to its existing `Tracks` row already (by
filename and size, via `ITrackDatabase::updateScannedTrackData`). The same match against
`MixRecovery` is not done. Where it would help: a mix that was captured **complete**, whose tracks
were deleted afterwards. The record holds the `filename`, `folderPath` and `filesizeBytes` those
tracks had, so a scan could recognise the file and re-attach it to the mix it was captured from.

**What it cannot recover**: the 95 mixes damaged before `MixTracks.track_id` had a foreign key.
Capture reads what survives in `MixTracks` (`Database/Sqlite/SqliteMixManager.cpp:635`, LEFT JOIN onto
`Tracks`) and stores only that (`:791`), so for rows that were already gone there is no filename, path
or size in the record - only the gap in `source_order_in_mix` saying something was there.

**Why it was not done with the rest**: re-attaching to a mix is a different decision from re-identifying
a row. It writes to `MixTracks` at a stored `source_order_in_mix` that may now collide with a surviving
row, and it needs a rule for what to do when only some of a mix's lost tracks come back. Neither
question arises for the `Tracks` case.

**Key files**: `Database/TrackScanner.cpp`, `Database/Sqlite/SqliteTrackDatabase.cpp`.
