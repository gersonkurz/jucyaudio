# JucyAudio - Open Tasks

Ordered by priority: **P1** (fix before tagging 2.0) → **P3** (whenever). One P1 is open - the macOS half of the
JUCE 9.0.2 upgrade has not been built. The memory-safety and deadlock items are done, and so is the
schema divergence that left every new library without search, markers and the EQ/reverb presets. P2 items are reachable correctness or
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

## P2: exported MP3s carry an unfinished Info frame and a duplicate one at the end

**Where it came from**: raised as a `[task]` finding by the reviewer of the JUCE 9.0.0 -> 9.0.2
upgrade (codex, 2026-09-13), thread `01a09bbf-582a-7443-80ea-02e320a39761`, after the same mistake
was found and fixed in that change's new test fixture. Recorded verbatim:

> **[task]** `Audio/ExportMixToMp3.cpp:263-274` writes the completed LAME Info frame at EOF after
> flushing. LAME requires that frame to replace the placeholder immediately after the ID3v2 tag.
> Exported MP3s therefore retain an unfinished initial Info frame and append a duplicate near EOF,
> making duration, seeking, and gapless metadata unreliable. This is deferrable because the export
> implementation predates the JUCE upgrade and is untouched by this change. Track a fix that records
> the placeholder offset, seeks back to overwrite it, restores the EOF position for ID3v1, and
> executes an export-format regression check.

**Why it is deferrable**: it predates the upgrade and nothing in that change touches the exporter.
It is P2 rather than P3 because it is a live, user-visible defect in shipped output rather than
something that cannot happen today: every MP3 this application has exported with a VBR tag enabled
carries it, and the symptom - a wrong duration, seeking that lands in the wrong place - shows up in
whatever player the user opens the file in, not in jucyaudio.

**What the same mistake looked like next door**: the audio format self test's MP3 fixture prepended
the finished tag frame instead of overwriting the placeholder. The resulting file held 42 frames
while its own Xing header declared 40, with exactly one extra 417-byte placeholder frame between the
real tag and the audio. That is the shape to look for when checking an exported file.

**Fix approach**: as the finding says - record the offset the placeholder was written at, seek back
to overwrite it once `lame_encode_flush` has run, then restore the end-of-file position so the ID3v1
tag still lands last. `m_outputStream` is a `juce::FileOutputStream`, so it can seek. The regression
check should parse an exported file and assert one Xing/Info frame, a declared byte count matching
the bytes actually present, and a declared frame count matching the frames actually parsed.

**Key files**: `Audio/ExportMixToMp3.cpp:223`, `:257`, `:263-274`.
