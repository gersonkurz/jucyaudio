# JucyAudio - Open Tasks

Ordered by priority: **P1** (fix before tagging 2.0) → **P3** (whenever). Nothing is P1 right now:
the memory-safety and deadlock items are done. P2 items are reachable correctness or user-visible
defects that are not memory-unsafe; P3 items cannot happen today, are bounded to a logged
stale-state effect, or need a design decision first.

---

## P2: a database created from scratch is missing six tables the migrations create

**Symptom**: `initialSqlStatements` (`Database/Sqlite/SqliteTrackDatabase.cpp:171` onwards) does not
create `TrackMarkers`, `TracksSearchData`, `TracksSearchFTS`, `MixMarkers`, `EQPresets` or
`ReverbPresets`, nor the EQ/reverb factory rows. Those exist only in the v4, v12, v16, v17 and v18
migration rungs, which a brand-new database never runs - `createTablesIfNeeded` stamps a new database
at the latest version and skips the ladder entirely. Confirmed against a database the self test
creates from scratch: `sqlite_master` holds sixteen tables and none of those six.

**What it costs**: full-text search, track markers, mix markers and the EQ/reverb presets do not work
in a library created with the current code, and fail rather than degrade. Every search path in
`Database/Sqlite/SqliteStatementConstruction.cpp` (`:267`, `:353`, `:414`, `:474`) joins
`TracksSearchFTS`; every statement in `Database/Sqlite/SqliteMarkerManager.cpp` names `TrackMarkers`;
`SqliteMixMarkerManager`, `SqliteEQPresetManager` and `SqliteReverbPresetManager` are in the same
position. A library that was upgraded from an older version has all six and is unaffected, which is
why this is not more visible.

**How it was found**: the v31 folder merge reuses the v24 track de-duplication SQL, which remaps
`TrackMarkers` and rebuilds the FTS index. It failed with `no such table: TrackMarkers` against a
fresh-schema fixture. The v31 rung now skips the steps whose table is absent, so it copes; the
divergence itself is untouched.

**Reviewer finding, verbatim** (codex, review of the v31 folder path index, 2026-09-01):

> **[task]** Expand the new schema-divergence task: [initialSqlStatements](C:/Projects/jucyaudio/Database/Sqlite/SqliteTrackDatabase.cpp:206) also omits `MixMarkers`, `EQPresets`, and `ReverbPresets`, not only `TrackMarkers`, `TracksSearchData`, and `TracksSearchFTS`. These additional tables, their indexes, and EQ/reverb factory rows exist only in the v16-v18 migration rungs at [SqliteTrackDatabase.cpp:2024](C:/Projects/jucyaudio/Database/Sqlite/SqliteTrackDatabase.cpp:2024), so fresh latest-version databases never create them and the corresponding managers fail when used. This predates v31 and is deferrable because repairing complete fresh/upgraded schema convergence requires a separate migration.

**Fix approach**: the three-place rule from CLAUDE.md, in the direction it is usually needed the
other way round - the six tables, their indexes, the v25 FTS sync triggers and the EQ/reverb factory
rows belong in `initialSqlStatements`, and a migration rung has to create them for the databases
already created without them (`CREATE TABLE IF NOT EXISTS` plus a one-off FTS `rebuild`). Walk the
whole ladder while doing it, since nothing enforces that a new database and a fully migrated one end
up with the same schema - a check that compares the two would be the thing that stops this
recurring.

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

## P3: the folder cache build ignores whether its reads worked

**Symptom**: `buildCacheIfNeeded` runs six statements and checks the result of one of them. The
`reserveFromCount` helper (`Database/Sqlite/SqliteFolderDatabase.cpp:53`) ignores a failed count, which
is harmless - it only sizes a container. The track-count and album pass is not: a failed or partial
read of `SELECT folder_id, COALESCE(artist_name, ''), ... FROM Tracks ORDER BY folder_ID ASC`
(`:247`) leaves folders with track counts that are too low or zero, and the cache is stamped valid at
the end (`:370`) either way. Seen for real while testing the `removeEmptyFolders` fix: with `Tracks`
made to fail, the build logged `no such table` and then carried on to mark the cache good.

**What it costs**: wrong track counts in the folder tree until something invalidates the cache - and
one thing that is written and stays written. The album pass decides a folder's album from the tracks it
has seen so far, and a read that stops early has seen a prefix: the folder is still pending when the
loop ends, so the tail block adds it (`:335`) and the transaction below writes it (`:349`), neither of
them having asked whether the read finished. A later track that would have disqualified that folder -
a different artist, an empty album title - was never reached. The Albums row that results is wrong and
survives every later cache rebuild, because the rebuild finds an album already there and leaves it
alone. Nothing is deleted, unlike the same pattern in `removeEmptyFolders`, which is fixed.

**Fix approach**: `hasError()` after each read that feeds the cache; refuse to mark the cache valid when
one of them failed, and write no albums from a pass that did not finish - `buildCacheIfNeeded` already
returns false on other kinds of inconsistency, so the shape exists. The album write is the urgent half:
a wrong track count is corrected by the next rebuild, a wrong Albums row is not. Worth doing together
with the accessor window below, since both are about a cache that reports a confident answer it does
not have.

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
