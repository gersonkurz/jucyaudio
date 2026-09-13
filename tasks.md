# JucyAudio - Open Tasks

Ordered by priority: **P1** (fix before tagging 2.0) → **P3** (whenever). Two P1s are open: the macOS
half of the JUCE 9.0.2 upgrade has not been built, and it needs a machine nobody has had to hand yet;
and the MP3 exporter reads past a stack buffer when the ID3v2 tag it is handed is larger than 10 KiB.
The deadlock items are done, and so is the schema divergence that left every new library without
search, markers and the EQ/reverb presets. P2 items are reachable correctness or user-visible defects
that are not memory-unsafe; P3 items cannot happen today, are bounded to a logged stale-state effect,
or need a design decision first.

Reprioritized on 2026-09-13, after reading every entry against the code it describes. Two moved. The
MP3 Info frame entry went P2 -> P1 and is **fixed in that same change**, so it is gone from this file
rather than sitting here relabelled. The folder cache entry went P3 -> P2; its own section says why.
The oversized-ID3v2 entry was added the same day by the reviewer of that fix, and is P1 rather than
the P2 it was first filed as: it is an out-of-bounds read, and the line above says in as many words
that P2 is for defects which are **not** memory-unsafe.

---

## P3: no executed check that a refused MP3 write discards the partial

**Symptom**: the WAV render's write propagation is now covered - a stream that refuses mid-render makes
the mixing loop fail, the partial is discarded and the previous export survives, all asserted in the
scan suite. The MP3 side has the same guards and none of them has ever run.

Be precise about what is missing, because it is not the writes. `ExportMp3MixImplementation` makes
five `m_outputStream->write` calls, named rather than numbered because the numbers have already gone
stale once. One is in setup: the ID3v2 tag, in `onSetupAudioFormatManagerAndWriter`. The other four
are the render and its finalisation - the encoded-buffer write inside `onRunMixingLoop`, the flush
write after `lame_encode_flush`, the LAME info frame write inside the `infoBytes > 0` block, and the
ID3v1 footer write after it.

Every one of those five **is** executed, by any successful export, and the tagged export check in the
timeline suite executes all five on purpose. What has never executed is the branch each one guards:
the `return fail(...)` a refused write leads to. Nothing can make the stream refuse, so none of those
five failure paths, and none of the partial-discarding behaviour they are supposed to trigger, has
been run even once.

The MP3 checks that exist cover cancellation, a pre-render source failure and the format of a
successful export. None of them reaches a refused write.

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

**Not unblocked by the Info frame fix** (corrected 2026-09-13): an earlier version of this note
claimed the two wanted doing together because both needed `m_outputStream` opened up. That was wrong
about the second one. Fixing the Info frame only needed the exporter to seek within its own stream,
which it can do from inside the class, and its regression check reads the finished file rather than
injecting anything. So this entry still needs what it always needed - a way to put a refusing stream
*into* `ExportMp3MixImplementation` - and nothing done since has provided it.

**Verified still current on 2026-09-13**: all five writes are still there and every refusal branch is
still unexecuted, `m_outputStream` is still `std::unique_ptr<juce::FileOutputStream>` and private to
`ExportMp3MixImplementation` (`Audio/ExportMixToMp3.h`), and `releaseOutput()` still calls
`getStatus()` on it. The Info frame change moved the info frame write inside a new `infoBytes > 0`
block but added no write - there were five before it and five after - and did nothing to make any
refusal injectable.

---

## P2: nothing can tell a folder cache that built from one that failed

**Why P2 and not P3** (moved 2026-09-13): P3 in this file means a thing that cannot happen today,
or whose effect is bounded to a logged stale-state. Neither holds here. Twelve reachable paths reach
it, and the effect is not bounded to a log line - the accessors hand back folders read out of a
half-filled map, which is wrong data served as truth. That is the P2 definition: a reachable
correctness defect that is not memory-unsafe. It stays below the macOS P1 because it needs a database
that is already failing its reads, which is not the common case.

**Symptom**: `buildCacheIfNeeded` (`Database/Sqlite/SqliteFolderDatabase.cpp:20`) returns `bool` and
now has **twelve** paths that return `false`. No caller looks at any of them.

The twelve, by kind:

- the folder read cannot be prepared (`:77`), or stops early (`:116`);
- the folder tree does not hold together - a duplicate path (`:171`), a missing parent (`:159`), a
  missing parent chain (`:181`), a visited-count mismatch (`:206`);
- the album read fails (`:244`) or stops early (`:267`);
- the track read fails (`:284`) or stops early (`:399`);
- the album write cannot commit (`:436`) or cannot begin (`:444`).

`m_isCacheValid` is set true at `:448` and nowhere else, so a build that took any of those twelve
exits leaves the cache invalid - correctly. What it also leaves behind is the problem: the four maps
are cleared at the *start* of the build (`:48`-`:51`) and filled as it goes, and not one of the twelve
exits clears them again. So a folder comes back from a build that failed exactly as it does from one
that worked, out of a map holding however much was read before the failure.

Every caller discards the answer. `initialize()` (`Database/Sqlite/SqliteFolderDatabase.h:45`),
`getAllChildFolders` (`:470`), `getFolderById` (`:531`), `hasChildren` (`:544`), `getParentSet`
(`:568`), `getChildFolders` (`:585`), `removeEmptyFolders` (`:814`) and `findOrCreateFolderByPath`
(`:852`).

**What it costs**: the failure is real - every later access rebuilds and fails again, and the log
fills up - but nothing in the process, and nothing a test can reach, reports it. `connect()` succeeds
against a database whose cache cannot be built at all.

**What changed since this was written** (updated 2026-09-13): commit `e0eed9a` added seven of those
twelve exits, by making each read that feeds the cache say how it ended. That commit fixed the
half of this that could *write* - no album is written from a pass that did not finish - so the
remaining damage is reads served from a half-filled map, not rows invented in the database.

One sub-claim here has narrowed and is worth stating precisely, because the entry used to overstate
it. `removeEmptyFolders` does now refuse when the read that decides what to delete fails, and the
folder cache suite asserts exactly that. What it still does unconditionally is the *rebuild after the
commit* (`:814`): if that fails, `removeEmptyFolders` returns `true` regardless.

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

**Symptom**: the cache accessors call `buildCacheIfNeeded()`, which returns having released both
mutexes, and then take `m_cacheMutex` for the read itself. The gap is one statement wide in each
(`Database/Sqlite/SqliteFolderDatabase.cpp`, build then lock): `getAllChildFolders` `:470`/`:474`,
`getFolderById` `:531`/`:532`, `hasChildren` `:544`/`:545`, `getParentSet` `:568`/`:569`,
`getChildFolders` `:585`/`:586`. An `invalidateCache()` landing in that gap empties the maps, and the
read reports the folder as absent - a folder that momentarily has no children, or no name, in the
middle of navigation.

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

**Symptom**: a reload that the timeline was not told about leaves it showing the rows it was built
from. `UI/DataViewComponent.cpp` calls `refreshCache(true)` from four live places - `:291`, `:800`,
`:811`, `:822` - and none of them tells the timeline. Editing that stale picture is refused - the five
write paths call `refuseIfViewsAreStale` (defined at `UI/TimelineComponent.cpp:1087`, called from
`:107`, `:371`, `:554`, `:677` and `:1401`), which compares
`MixProjectLoader::getContentsGeneration()` against the generation stamped at populate time (`:846`,
`:1846`), logs, and schedules a repopulation - so nothing writes to a row it did not mean. What is
left is the display: until the user tries to edit, the timeline shows the previous contents and
nothing corrects it.

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
Capture reads what survives in `MixTracks` (`Database/Sqlite/SqliteMixManager.cpp:640`, LEFT JOIN onto
`Tracks`, and `:641` onto `Folders`) and stores only that (the `INSERT INTO MixRecovery` at `:810`),
so for rows that were already gone there is no filename, path or size in the record - only the gap in
`source_order_in_mix` saying something was there.

**Why it was not done with the rest**: re-attaching to a mix is a different decision from re-identifying
a row. It writes to `MixTracks` at a stored `source_order_in_mix` that may now collide with a surviving
row, and it needs a rule for what to do when only some of a mix's lost tracks come back. Neither
question arises for the `Tracks` case.

**Key files**: `Database/TrackScanner.cpp`, `Database/Sqlite/SqliteTrackDatabase.cpp`.

---

## P3: macOS links both JUCE's embedded zlib and system `libz`

**Where it came from**: raised as a `[task]` finding by the reviewer of the JUCE 9.0.0 -> 9.0.2
upgrade (codex, 2026-09-13), thread `01a09bbf-582a-7443-80ea-02e320a39761`. Recorded verbatim:

> **[task]** `CMakeLists.txt:735`, `:759`, and `:763` compile JUCE's embedded zlib while also
> linking static TagLib and system `libz` on macOS. JUCE documents this configuration as risking
> symbol conflicts, ODR violations, or linker errors. The defect is deferrable because JUCE 9.0.0's
> CMake integration already compiled `juce_core_zlib.c`, so this upgrade did not introduce it. Track
> a follow-up to choose one zlib implementation - normally macOS-only `JUCE_INCLUDE_ZLIB_CODE=0`
> with the system headers/library - and validate both macOS architectures plus compressed-ID3
> decoding. [JUCE breaking-change guidance](https://github.com/juce-framework/JUCE/blob/9.0.2/BREAKING_CHANGES.md#version-901)

**Why it is deferrable**: it predates the 9.0.2 bump. `modules/juce_core/juce_core_zlib.c` already
exists as a C translation unit at the `9.0.0` tag with the same `#include ".../zlib/deflate.c"` set,
and `extras/Build/CMake/JUCEModuleSupport.cmake` differs between the two tags only by the new
`juce_gui_extra` webview-interop block. So the un-namespaced zlib symbols are the status quo on
macOS, not something the upgrade introduced. Windows is unaffected: the configure log says
`Could NOT find ZLIB (missing: ZLIB_LIBRARY ZLIB_INCLUDE_DIR)`, so TagLib builds without zlib there
and nothing puts a system zlib on the Windows link line.

**Fix approach**: set `JUCE_INCLUDE_ZLIB_CODE=0` plus `JUCE_ZLIB_INCLUDE_PATH` on macOS only, so the
one system `libz` serves both JUCE and TagLib. Validate on arm64 and x86_64, and specifically
exercise compressed-ID3 decoding, which is the path that actually calls into zlib.

---

## P1: the JUCE 9.0.2 upgrade has not been built or self-tested on macOS

**What is missing**: `just build && just selftest` on macOS against the `9.0.2` pin. Windows x64
Release is green - all eight suites pass, including the new audio format suite that proves the two
decoder behaviours the upgrade was taken for. No macOS machine was available to the session that
made the change, so that half of the cross-platform invariant in `CLAUDE.md` and the "successful
non-GUI builds on Windows and macOS paths" release gate in `docs/release-plan-2.0.md:37` is
unverified.

**How it was decided**: raised as a `[blocking]` finding by the reviewer of the upgrade (codex,
2026-09-13), thread `01a09bbf-582a-7443-80ea-02e320a39761`, which said to run it on macOS or to
resolve it through the protocol's explicit human deferral path. The human chose to defer it
explicitly on 2026-09-13, accepting the risk on the record, and this entry is the follow-up that
deferral requires.

**Why the risk is bounded rather than unknown**: the change is a dependency version, three
documentation lines, and a new self test suite. No production C++ changed, and the macOS link block
in `CMakeLists.txt` is untouched. The test code is cross-platform and is wired into the macOS
`selftest` recipe as well as the Windows one, so it is part of what an eventual macOS run covers. All
five breaking changes in JUCE 9.0.1 and 9.0.2 were checked against this tree and four cannot apply
(`AudioDeviceSelectorComponent::getMidiInputSelectorListBox`, `OpenGLImageType`,
`OpenGLContext::setImageCacheSize` and the `WebBrowserComponent` package move, the last of which is
moot under `JUCE_WEB_BROWSER=0`). The fifth is the zlib linkage recorded separately below, which
predates this upgrade.

**What would actually catch something**: the CoreAudio changes in 9.0.2 - the default sample rate
and buffer size selection, and Multi-Output device handling. Those are macOS-only, are not exercised
by any self test on any platform, and need a human at a Mac with an audio device. The build and self
test are the floor, not the whole check.

---

## P1: an oversized ID3v2 tag makes the MP3 exporter read past its stack buffer

**Where it came from**: raised as a `[task]` finding by the reviewer of the Info frame fix (codex,
2026-09-13), thread `01a09c2e-0c00-7d83-ad8a-ca4039c77af3`. Recorded verbatim:

> **[task]** `ExportMixToMp3.cpp:87` uses a fixed 10 KiB ID3v2 buffer without checking whether
> `lame_get_id3v2_tag()` returned a larger required size. In that case LAME copies nothing, but line
> 89 reads `id3bytes` from the smaller stack array, causing an out-of-bounds read that can crash or
> place unrelated stack data in the export. The multiline comment input is not length-limited. This
> predates the reviewed diff and is distinct from placing the finished Info frame, so it is
> deferrable; record a task to query/allocate the required size or reject an oversized tag before
> writing.

**Confirmed against LAME's contract**: `lame.h` says of `lame_get_id3v2_tag` - "Function returns
number of bytes copied into buffer, or number of bytes rquired if buffer 'size' is too small.
Function fails, if returned value is larger than 'size'." So on overflow the return is a size, not a
count, nothing was written into the array, and handing that number to `write()` reads whatever
follows a 10 KiB stack buffer straight into the user's file.

**Why P1**: it is an out-of-bounds read, and this file reserves P2 for defects that are *not*
memory-unsafe. P3's "cannot happen today" does not apply either - it needs only a long enough tag,
and the comment field has no length limit anywhere between the settings dialog and here. What it
costs is either a crash or a slice of this process's stack written into a file the user then hands to
someone else. Reaching 10 KiB of ID3v2 takes a deliberately large comment rather than ordinary use,
which is the one thing keeping it from being the first item in this file.

**Fix approach**: the guard the same function already applies to the info frame - compare the return
against the buffer size and fail before writing - or size the buffer from a first call with
`size` zero and allocate. The first is three lines and mirrors code that is already there; the second
is better behaviour, since it exports the tag the user asked for rather than refusing. The same
question applies to `lame_get_id3v1_tag`, though ID3v1 is a fixed 128 bytes so that one cannot
overflow.

**Not fixed with the Info frame change** because the review protocol keeps `[task]` findings out of
the change they were raised against, and the human should get the choice between refusing and
allocating.
