# JucyAudio - Open Tasks

Ordered by priority: **P1** (fix before tagging 2.0) → **P3** (whenever). P1 items are memory-safety
or deadlock bugs reachable in today's code; P2 items are reachable correctness or user-visible
defects that are not memory-unsafe; P3 items cannot happen today, are bounded to a logged
stale-state effect, or need a design decision first.

---

## P1: `SqliteFolderDatabase` lock-order inversion

**Symptom**: Two threads can deadlock. The cache accessors (`getFolderById`, `hasChildren`,
`getParentSet`, `getChildFolders`, `getAllChildFolders`) call `buildCacheIfNeeded()`, which takes
`m_cacheMutex` (`Database/Sqlite/SqliteFolderDatabase.cpp:22`) and then acquires the database mutex
through `SqliteStatement`. `findOrCreateFolderByPath` (`:660`) takes the database mutex first and then
wants `m_cacheMutex`.

**Why it has not bitten yet**: `buildCacheIfNeeded` only reaches for the database mutex when the cache
is invalid. That happens during scans — which is exactly when `findOrCreateFolderByPath` runs hardest,
so the window is narrow rather than absent.

**Fix approach**: one consistent order, database mutex before cache mutex, on both paths. Note while
in there: `findOrCreateFolderByPath` also reads `m_idFromFolderPath` and `m_folderInfoFromId` and
writes `actualPath` back into the cache while holding only the database mutex.

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
