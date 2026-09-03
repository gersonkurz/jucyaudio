# JucyAudio - Open Tasks

Ordered by priority: **P1** (fix before tagging 2.0) → **P3** (whenever). Nothing is P1 right now:
the memory-safety and deadlock items are done, and so is the schema divergence that left every new
library without search, markers and the EQ/reverb presets. P2 items are reachable correctness or
user-visible defects that are not memory-unsafe; P3 items cannot happen today, are bounded to a
logged stale-state effect, or need a design decision first.

---

## P3: no executed check that a refused MP3 write discards the partial

**Symptom**: the WAV render's write propagation is now covered - a stream that refuses mid-render makes
the mixing loop fail, the partial is discarded and the previous export survives, all asserted in the
scan suite. The MP3 render's four writes (`Audio/ExportMixToMp3.cpp:223`, `:257`, `:266`, `:274`) are
checked in the same way and none of it is executed. The MP3 checks that exist cover cancellation and a
pre-render source failure, neither of which reaches a refused write.

**Why it was not covered with the WAV half**: the technique that works for WAV does not transfer. WAV
writes through a `juce::AudioFormatWriter` built over a `juce::OutputStream`, so the self test
substitutes the stream and changes nothing else. MP3 writes through `m_outputStream`, a
`std::unique_ptr<juce::FileOutputStream>` that is private to `ExportMp3MixImplementation`, and
`releaseOutput()` calls `getStatus()` on it - a `FileOutputStream` method, so the member cannot simply
become a `juce::OutputStream`. Covering it needs that private member opened up as well, or a
`juce::FileOutputStream` subclass installed into it, which is a deeper concession than the WAV half
cost.

**How it was decided**: raised as a [blocking] finding by the reviewer of the WAV write-refusal check
(codex, 2026-09-03), which said either to cover MP3 too or to have the human defer it explicitly and
keep this entry. The human had already been told, in the option they chose when picking how to cover
WAV, that MP3 "writes through LAME and would need its own treatment, so it stays review-verified
unless you want that too", and chose that option. So the risk is accepted rather than overlooked, and
recorded here rather than closed.

**Fix approach**: either the shared seam the earlier entry described - one virtual returning the render
output stream, used by both mixing loops - or the same subclassing trick with
`ExportMp3MixImplementation` made non-final and `m_outputStream` protected, plus a test-only
`juce::FileOutputStream` whose `write()` refuses after N bytes. The check should assert the same three
things the WAV one does, and should pin the step that failed rather than only that the export failed.

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
