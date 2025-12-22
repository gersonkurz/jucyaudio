# Release Review: Dialogs and Major Functionality

## Review Findings (Initial)
### High
h1 - DONE - Potential invalid mix data for short tracks when appending to an existing mix: fixed 5s/2s crossfade math can yield negative `attachTo`/envelope times if `trackInfo.duration < 5000ms`, which can propagate into playback/export and timeline math. `UI/CreateMixDialogComponent.cpp:251`
h2 - DONE - Use-after-free risk from async UI callbacks in long-running tasks: background thread calls `MessageManager::callAsync` capturing raw `this` without a `Component::SafePointer`, so closing the dialog while a task is running can crash. `UI/TaskDialog.cpp:185`
h3 - N/A - MP3 export assumes stereo when building interleaved samples; `outputNumChannels()==1` still reads channel 1 and interleaves stereo data, which can crash or encode garbage for mono exports. `Audio/ExportMixToMp3.cpp:170` - FALSE POSITIVE: outputNumChannels() is hardcoded to 2, mono export path never executes
h4 - DONE - BPM analysis can detach worker threads after a timeout; detached threads can keep running and access task-owned data after completion, risking use-after-free. `Database/BackgroundTasks/BpmAnalysisTask.cpp:356`

### Medium
m1 - Export tag settings can silently desync from the visible fields: switching away from MP3 clears `m_settings.*`, but switching back to MP3 does not repopulate settings from the still-visible text editors unless the user re-types, resulting in empty tags on export. `UI/ExportMixDialog.cpp:297`
m2 - Async UI callbacks capture raw `this`/`TreeViewItem*` without lifetime guards; if the tree rebuilds or the component closes before the callback runs, it can dereference freed objects. `UI/NavigationPanelComponent.cpp:433`
m3 - Render-thread tile updates schedule a repaint per tile via `callAsync` without a lifetime guard, which risks use-after-free on teardown and can flood the message queue during heavy rendering. `UI/VirtualTimelineComponent.cpp:2518`
m4 - Export mixing loops ignore read failures from `contributeFromActiveSource`, so I/O errors can silently produce partial silence while the export still reports success. `Audio/ExportMixToWav.cpp:97`, `Audio/ExportMixToMp3.cpp:143`
m5 - MP3 export allocates an interleaved buffer on every block; this per-block heap allocation is avoidable and can significantly slow large exports. `Audio/ExportMixToMp3.cpp:168`
m6 - Background task loop sleeps for 500ms even when work is available; this adds unnecessary latency to scanning/analysis tasks and makes progress feel sluggish. `Database/BackgroundService.cpp:80`
m7 - Export mixing allocates per-track temporary buffers on every block, which can cause significant heap churn on large mixes and slow exports. `Audio/ExportMixImplementation.cpp:405`
m8 - Track scan missing-file handling is effectively disabled when `removeMissingFiles` is false; the code that should mark missing tracks is commented out, so missing files remain marked as present. `Database/TrackScanner.cpp:225`
m9 - Timeline reorder reload uses `MessageManager::callAsync` capturing raw `this` and assumes `m_mixLoader` stays valid; if the component is closed or reloads while pending, this can UAF/crash. `UI/TimelineComponent.cpp:1142`
m10 - Master EQ/Reverb processing uses an `AudioBlock` that spans from `startSample` to the end of the buffer, not just `numSamples`, so DSP runs on data outside the active region and can corrupt audio. `UI/PlaybackController.cpp:64`
m11 - Cue point edits don’t reload playback state and bypass the read-only check, so the UI can update while playback uses stale data and read-only mixes can still be modified. `UI/MixEditorComponent.cpp:681`
m12 - M3U export builds the “artist - title” string with a malformed format call, so titles are omitted and output lines are incorrect. `Audio/ExportMixToM3U.cpp:86`
m13 - Mix playback/export ignore cueStart/cueEnd and always use full track duration, so edits to cue points don’t affect actual audio output. `Audio/ExportMixImplementation.cpp:363`, `Audio/MixPlaybackEngine.cpp:636`
m14 - Navigation tree assumes the root has at least one child; `children.front()` is called without checking for empty, which can crash on an empty database. `UI/NavigationTree.cpp:34`
m15 - Post-restore UI reinitialization is scheduled with `callAsync` capturing raw `this`; closing the main window before the async runs can UAF. `UI/MainComponent.cpp:3520`
m16 - Library root scan completion posts nested `callAsync` callbacks capturing raw `this`, risking UAF if the component closes during a scan. `UI/LibraryRootsComponent.cpp:505`
m17 - Database backup task calls `performBackupCheck` with the wrong signature (missing database path and flags), so backups may never run or this unit fails to compile if enabled. `Database/BackgroundTasks/DatabaseBackupTask.cpp:27`
m18 - Thread safety violation in cue point updates: `updateCuePointsInData` uses `const_cast` to modify `MixProjectLoader` data that may be accessed concurrently by the audio playback thread, risking data races or corrupted playback. `UI/MixEditorComponent.cpp:693`
m19 - Export loop silently ignores errors: `contributeFromActiveSource()` return value is discarded in MP3 export loop; I/O failures produce silence but export reports success. `Audio/ExportMixToMp3.cpp:143`
m20 - DatabaseBackupTask signature mismatch: Call `performBackupCheck(m_settings, true)` doesn't match the 5-parameter signature `(RootSettings&, path, bool, bool, bool)`. This is likely a compile error or dead code path. `Database/BackgroundTasks/DatabaseBackupTask.cpp:27`
m21 - PlaybackController AudioBlock spans wrong region: DSP block is created from `startSample` to buffer end instead of using `numSamples`, processing data outside the active region. `UI/PlaybackController.cpp:114-117`
m22 - NavigationTree crashes on empty database: `children.front()` called without checking `children.empty()` after `m_root->expand()`. `UI/NavigationTree.cpp:47`
- Cue point edits bypass read-only check: `updateCuePointsInData()` has no `m_isReadOnly` guard unlike `updateCueAttachInData()`, allowing modification of exported/locked mixes. `UI/MixEditorComponent.cpp:681`

### Low
l1 - Dialogs use `MessageManager::callAsync` for focus after show, capturing raw `this` without a guard; rapid close paths could still race to a dangling pointer. `UI/CreateMixDialogComponent.cpp:82`, `UI/CreateWorkingSetDialogComponent.cpp:75`, `UI/ExportMixDialog.cpp:123`
l2 - Export pipeline logs mono-track debug at info level during normal operation, which can add overhead and bloat logs on large exports. `Audio/ExportMixImplementation.cpp:381`
l3 - Export resampling uses simple linear interpolation without low-pass filtering, which can introduce aliasing on rate conversion. `Audio/ExportMixImplementation.cpp:445`
l4 - Missing-file reporting counts folders, not tracks; `existingTrackCache.size()` is the number of folder buckets, and empty buckets keep the map non-empty, so logs can claim missing tracks even when none remain. `Database/TrackScanner.cpp:181`
l5 - Mix editor resize logs detailed timing at info level on every resize, which can hurt responsiveness and spam logs during window drags. `UI/MixEditorComponent.cpp:548`
l6 - Per-track update path (`updateMixTrack`) doesn't recompute mix total duration; cue-only edits can leave mix summaries stale. `Database/Sqlite/SqliteMixManager.cpp:676`
l7 - Mix number counter can skip values: `getNextMixNumber()` called in dialog constructor, but `incrementMixNumber()` only called on success; canceling and reopening may show skipped numbers. `UI/CreateMixDialogComponent.cpp:374`
l8 - Inconsistent null checks for `getTrackDatabase()`: Some call sites check for null, others don't. `UI/CreateMixDialogComponent.cpp:216` checks, but similar patterns elsewhere may not.
l9 - Debug logging at INFO level: Several places log debug-level messages as INFO during normal operation, bloating logs. `Audio/ExportMixImplementation.cpp:489`, `UI/PlaybackController.cpp:218`

### Dependencies and Licensing
The following third-party libraries are in use and need license files for distribution:
- **JUCE** - GPLv3 or Commercial (requires license file or commercial license)
- **LAME** - LGPL 2.0 (requires attribution and source availability notice)
- **SQLite** - Public Domain (no requirements)
- **spdlog** - MIT License (requires copyright notice)
- **ICU** - ICU License (requires copyright notice)
- **aubio** - GPLv3 (viral license - needs evaluation)
- **libsndfile** - LGPL 2.1 (requires attribution)
- **FFTW** - GPL (viral license - needs evaluation)
- **TagLib** - LGPL/MPL dual license (requires attribution)
- **tomlplusplus** - MIT License (requires copyright notice)
- **pthreads-w32** - LGPL (Windows only, requires attribution)

The entire app IS GPL licensed, so no worries here.
