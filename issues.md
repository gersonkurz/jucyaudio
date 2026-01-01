# JucyAudio - Issues & Code Review Findings

## Existing Feature Requests / Ideas

- If not all mixes can be shown because a folder is unmounted, show them hidden - if selected, instead of showing data, show a special screen "not available because XYZ is not mounted"
- Support being able to right-click on a file and play it in jucyaudio. Might have to add this file then to the database in case it does not exist yet?
- migrate from spdlog to quill - more logging, less performance impact.
- long-term task: support for folder reorganization needs to be improved

## New issues found while getting ready for the 1.0 release

### Critical / High Priority

1) **Buffer Overflow Risk in Audio Engine**
   - **Description:** `MixPlaybackEngine::mixActiveTracksForBlock` uses `m_scratchBuffer`, which is pre-allocated to `samplesPerBlockExpected` in `prepareToPlay`. If the host requests more samples than this (possible in some hosts/standards), `m_scratchBuffer` will overflow.
   - **Location:** `Audio/MixPlaybackEngine.cpp`
   - **Fix:** Add safety check in `getNextAudioBlock` to cap samples or handle reallocation.

2) **Double-Buffer Use-After-Free**
   - **Description:** Rapid calls to `loadMix()` can cause a use-after-free. `retireState` deletes `m_previousState` (the state from 2 swaps ago). If the audio thread is still processing a block using that old state when a new `loadMix` happens quickly, it might access deleted memory.
   - **Location:** `Audio/MixPlaybackEngine.cpp:159`, `Audio/MixPlaybackEngine.cpp:296`
   - **Fix:** Use a safer reclamation strategy (e.g., `std::shared_ptr` or a garbage collector queue for old states) instead of simple double-buffering.

3) **Dangling Pointer in SQLite Binding**
   - **Description:** `SqliteStatement::bindColumnFrom` calls `getBlob()`, which returns a temporary `std::vector`. It then passes this to `addParam` which uses `SQLITE_STATIC`, binding the statement to the data of the temporary vector that is immediately destroyed.
   - **Location:** `Database/Sqlite/SqliteStatement.cpp:28`
   - **Fix:** Change `addParam` for BLOBs to use `SQLITE_TRANSIENT` or ensure the vector persists.

### Medium Priority

4) **Resampling Synchronization Drift**
   - **Description:** When seeking, `MixPlaybackEngine` calculates the position for `readerSource` (source rate). However, `ResamplingAudioSource` maintains its own internal state. Setting `readerSource` directly can cause artifacts or desynchronization unless the resampler state is reset.
   - **Location:** `Audio/MixPlaybackEngine.cpp:436`
   - **Fix:** `ResamplingAudioSource` generally requires `prepareToPlay` to be called again or careful state management when its source is seeked.

5) **Negative `cueStart` Handling**
   - **Description:** `cueStart` can be negative (pre-silence), but read offsets are clamped to `>= 0`. This effectively ignores the pre-silence intent.
   - **Location:** `Audio/ExportMixImplementation.cpp:411`, `Audio/MixPlaybackEngine.cpp:429`

6) **Mix Duration Calculation Mismatch**
   - **Description:** `MixProjectLoader::calculateMixDuration` uses `trackInfo.duration` (full file length) instead of `MixTrack::getEffectiveDuration` (which accounts for cues). This causes discrepancies between the displayed timeline and actual playback/export duration.
   - **Location:** `Audio/MixProjectLoader.cpp`

7) **Envelope Gain Clamping Inconsistency**
   - **Description:** Export logic clamps total gain (envelope * track gain) to [0, 1]. Real-time playback does not. This leads to "what you hear is NOT what you get" if gains are boosted.
   - **Location:** `Audio/ExportMixImplementation.cpp:269` vs `Audio/MixPlaybackEngine.cpp`

8) **Export I/O warnings are easy to miss (UX)**
   - **Description:** Export tracks read failures and logs them to spdlog, but the UI only shows "(with warnings)" in the progress message. Consider a user-visible warning dialog or a structured status object so warnings aren’t missed.

### Low Priority / Code Hygiene

9) **Redundant Database Cache Code**
   - **Description:** `SqliteFolderDatabase.cpp` contains three cache builder implementations (`Gerson`, `Codex`, `ClaudeCode`) and a `Compare` function with a dangerous `std::exit(0)`.
   - **Fix:** Remove unused implementations.

10) **Bad Dependency (Audio Math)**
    - **Description:** `interpolateVolumeFromEnvelope` is defined in `ExportMixImplementation.cpp` (offline) but used in `MixPlaybackEngine` (realtime) via `extern`.
    - **Fix:** Move to `Audio/AudioUtils.h`.

11) **Refactor: Extract crossfade calculation helper**
    - **Description:** The crossfade/envelope logic in `UI/CreateMixDialogComponent.cpp` (append to mix) and `Database/Sqlite/SqliteMixManager.cpp` (createAndSaveAutoMix) are near-duplicates. Extract to a shared helper like `Database/Includes/MixTrackUtils.h` with a `calculateCrossfadeForTrack()` function to avoid drift if thresholds or envelope curves change.

---

## Claude Code Review Findings

### Medium Priority

12) **SQL Injection in Filter Criteria**
    - **Description:** `SqliteStatementConstruction::addFilterCriteria()` directly concatenates user-supplied filter values into SQL queries without parameterization. A malicious filter like `year:1991' OR '1'='1` could bypass query constraints.
    - **Location:** `Database/Sqlite/SqliteStatementConstruction.cpp:148-217`
    - **Fix:** Convert to parameterized queries using `SqliteStatement::addParam()` like the rest of the SQL code.

13) **Missing Numeric Validation in FilterParser**
    - **Description:** `FilterParser::parseFilterString()` doesn't validate that numeric filter values (e.g., `year:`, `bpm:`) are actually numeric before passing them to the SQL layer. Combined with #12, this allows arbitrary strings to reach SQL construction.
    - **Location:** `Utils/FilterParser.cpp:107-176`
    - **Fix:** Add validation in `tryParseFilter()` that numeric fields contain only digits/decimal points.

14) **Equalizer Parameter Update Silently Dropped**
    - **Description:** `Equalizer::updateParameters()` uses `ScopedTryLockType`. If the lock acquisition fails (audio thread holds it), the UI's parameter update is silently discarded. Rapid UI adjustments may be lost.
    - **Location:** `Audio/Equalizer.cpp:45-57`
    - **Fix:** Use a lock-free approach (atomic flag + pending settings) or queue failed updates for retry.

15) **BackgroundService::pause() Uses Polling Loop**
    - **Description:** `pause()` busy-waits with a hard 1-second timeout (10 iterations × 100ms). If task processing exceeds this, `pause()` returns while work is still in progress, violating the caller's expectation.
    - **Location:** `Database/BackgroundService.cpp:61-68`
    - **Fix:** Use `std::condition_variable` to wait for `m_isProcessing` to become false, with proper timeout handling.

### Low Priority / Code Hygiene

16) **Duplicate typedef Declarations**
    - **Description:** `Constants.h` defines `TrackId` and `TagId` twice (lines 81-82 and 104-109). Harmless but clutters the code.
    - **Location:** `Database/Includes/Constants.h`
    - **Fix:** Remove duplicate declarations.

17) **Pointer-to-Integer Cast in getUniqueId()**
    - **Description:** `BaseNode::getUniqueId()` casts `this` pointer to `int64_t`. While currently safe on all common platforms, this is technically non-portable if pointers ever exceed 64 bits.
    - **Location:** `Database/Nodes/BaseNode.cpp:144`
    - **Fix:** Use a static atomic counter to generate unique IDs instead.

---

## Additional Findings (User Reported)

### Low Priority

18) **Logical vs Bitwise AND**
    - **Description:** `CreateMixDialogComponent::closeThisDialog` uses `&` instead of `&&`, which prevents short-circuiting and is likely unintended.
    - **Location:** `UI/CreateMixDialogComponent.cpp:345`
    - **Fix:** Replace `&` with `&&`.

### Review Notes

**Regarding #2 (Double-Buffer Use-After-Free):** The implementation uses a sound double-buffer pattern where the previous state survives one full swap cycle. However, the concern about "rapid `loadMix()` calls" is valid if swaps happen faster than audio block processing. The current design assumes swaps are infrequent relative to audio callback frequency.

**Regarding #3 (Dangling Pointer):** Confirmed as a real bug. The `SQLITE_STATIC` flag tells SQLite the blob data will remain valid, but `getBlob()` returns a temporary `std::vector` that's destroyed immediately after `addParam()` returns.

**Positive observations:**
- Core SQL operations properly use parameterized queries (only `addFilterCriteria()` breaks this pattern)
- Reference counting is consistently applied with debug tracing support
- Audio thread uses proper lock-free atomic swaps for state access
- Good separation into namespaces (`database`, `audio`, `ui`, `config`)
