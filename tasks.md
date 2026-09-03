# JucyAudio - Open Tasks

Ordered by priority: **P1** (fix before tagging 2.0) → **P3** (whenever). Nothing is P1 right now:
the memory-safety and deadlock items are done, and so is the schema divergence that left every new
library without search, markers and the EQ/reverb presets. P2 items are reachable correctness or
user-visible defects that are not memory-unsafe; P3 items cannot happen today, are bounded to a
logged stale-state effect, or need a design decision first.

---

## P3: the v29 MixRecovery fixture is not in the column order v29 really had

Recorded verbatim from the reviewer of the whole-ladder convergence change (round 2, finding 4):

> `Tests/SelfTests.cpp:2893` hand-writes the v29 `MixRecovery` fixture with `total_duration` fifth. The
> real migrations append `total_duration` and then `is_complete` at
> `SqliteTrackDatabase.cpp:3243` and `SqliteTrackDatabase.cpp:3281`, so the real v29 order ends with
> `mix_data, total_duration, is_complete`. This makes the fixture historically inaccurate and could
> mislead future ordinal-sensitive checks. It is deferrable because the v30 migration and current
> assertions address every field by name.

---

## P3: Albums and MixRecovery have different column order in a new database than in a migrated one

**Symptom**: the ladder appends `Albums.bitrate` (v14) and `MixRecovery.total_duration` (v28), while
`initialSqlStatements` declares both mid-table. So a database created from scratch numbers those
columns differently from one that migrated up. Measured, not suspected: 24 of 26 tables agree, these
two do not.

**Why it is P3 and not P2**: nothing reads either table with `SELECT *`. The two tables that are read
that way and decoded by position - `Tracks` via `trackInfoFromStatement` and `MixTracks` via
`readMixTracksChecked` - do agree, and the whole-ladder check in the migration self test compares
ordinals for exactly those two so they stay that way. The divergence is latent: it costs nothing until
someone writes `SELECT * FROM Albums`.

**Why it was not simply fixed**: reordering the fresh schema to match the ladder would leave every
database already stamped at the latest version on the old order and every new one on the new order,
with no rung able to tell them apart - the divergence this suite exists to prevent, introduced by the
attempt to remove one. Reordering the other way needs a rung that rebuilds both tables, which is the
riskiest kind of migration and buys no reader anything.

**Fix approach**: remove the positional dependency instead, so column order stops being a behaviour
anywhere. Give `SELECT * FROM Tracks` (`SqliteTrackDatabase.cpp:3884` and `:3931`) and
`SELECT * FROM MixTracks` (`SqliteMixSummary.cpp:88`) explicit column lists matching what
`trackInfoFromStatement` and `readMixTracksChecked` decode. Then the ordinal check can be dropped from
the convergence comparison altogether and this entry closes with it.

---

## P3: no executed check that a failed write discards the partial

**Symptom**: the WAV render now checks `writeFromAudioSampleBuffer` and `flush`, and the MP3 render
checks its ID3 and LAME-frame writes, so a write that fails makes the mixing loop fail, `run()`
discards the partial file, and the previous export survives. Nothing proves it. Every check in the
self test reaches the render failing *before* it starts - a track that cannot be resolved - which
exercises the same discard path but not the write propagation that leads to it.

**Why it is not covered**: a mid-render write failure needs a full disk or a failing device. Every
injection reachable from outside the exporter - a read-only partial, a partial path that is a
directory, an unwritable parent - is refused at the setup step instead, before any sample is written.
Inducing one properly needs a test-only seam in `ExportMixImplementation`, which is an affordance
this codebase does not have anywhere else.

**How it was found**: raised as a [blocking] finding by the reviewer of the failed-export
preservation change (codex, 2026-09-01), which asked for an executed fault-injection check. The
propagation fix shipped; the executed check was accepted as deferred by the human on the same day.

**Fix approach**: a seam is the honest way - a virtual the self test overrides to fail the Nth write -
and it is worth weighing against the alternative of leaving this to code review, since it is the only
place in the project that would carry one. If a seam is added it should be one hook, used by both
mixing loops, and the check should assert all three things at once: the export fails, the partial is
gone, and the file that was already there is untouched.

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

## P3: nothing can tell a folder cache that built from one that failed

**Symptom**: `buildCacheIfNeeded` returns `bool` and has five paths that return `false` - a duplicate
path, a missing parent, a missing parent chain, a visited-count mismatch, and a failed album write.
No caller looks at it. `initialize()` discards it (`Database/Sqlite/SqliteFolderDatabase.h`), and every
accessor - `getFolderById`, `hasChildren`, `getParentSet`, `getChildFolders`, `getAllChildFolders` -
calls it and then reads the maps regardless of the answer. `m_folderInfoFromId` and
`m_childrenFromParents` are filled in before all five of those paths, so a folder comes back from a
build that failed exactly as it does from one that worked. `removeEmptyFolders` calls it and returns
`true` either way.

**What it costs**: the failure is real - `m_isCacheValid` stays false, so every later access rebuilds
and fails again, and the log fills up - but nothing in the process, and nothing a test can reach,
reports it. `connect()` succeeds against a database whose cache cannot be built at all.

**How it was found**: writing a self test check for the v31 migration. The check asserted "the folder
cache builds against the migrated database" by asking `getFolderById` for a folder - and a probe that
deliberately broke the album write still passed it, because the map had already been populated. The
check was renamed to say what it really tests.

**Fix approach**: the cheap half is for `initialize()` to look at the result and log a distinct line
when a cache build fails, so at least the process says so. The useful half is for the accessors to
answer differently - a `std::optional` that is empty because the cache is broken, rather than because
the folder is not there - which is the same shape as the statusless-read entry above and probably
wants deciding together with it.

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
