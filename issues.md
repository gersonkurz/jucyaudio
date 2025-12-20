# JucyAudio - Issues & Code Review Findings

---

## Existing Feature Requests / Ideas

- If not all mixes can be shown because a folder is unmounted, show them hidden - if selected, instead of showing data, show a special screen "not available because XYZ is not mounted"
- Support being able to right-click on a file and play it in jucyaudio. Might have to add this file then to the database in case it does not exist yet?
- migrate from spdlog to quill - more logging, less performance impact.
- long-term task: support for folder reorganization needs to be improved

---

## Code Review Issues (December 2025)

Issues identified during code review, prioritized by severity.

---

### Issue 1: Use-After-Free on PlaybackState (HIGH)

**Location:** `Audio/MixPlaybackEngine.cpp` lines 279-293, 601, 146-158

**Problem:**
The audio thread reads `m_currentPlaybackState` via atomic load without retaining, while the UI thread can swap and delete the old state asynchronously. Race condition:

1. UI thread calls `loadMix()`, exchanges state atomically (line 280)
2. Old state queued in `m_statesToDelete` (line 288)
3. `cleanupOldStates()` scheduled via `MessageManager::callAsync` (line 291-293)
4. Audio thread still iterating over old state's `trackSources` when deletion occurs

**Fix:**
Use a hazard pointer pattern or double-buffering with epoch-based reclamation:

Option A - Deferred deletion with safe delay:
- Instead of immediate async cleanup, use a timer-based delay (e.g., 500ms) to ensure audio thread has moved on
- Simple but not deterministic

Option B - Atomic reference counting in audio thread:
- Audio thread calls `retain()` at start of `getNextAudioBlock()`, `release()` at end
- Requires making retain/release lock-free (currently uses atomic refcount, should work)

Option C - Triple buffering:
- Keep last N states alive, only delete when N+1 new state arrives
- Guarantees audio thread always has valid state

**Recommended:** Option B - proper retain/release in audio callback, ensuring the release is deferred to message thread.

---

### Issue 2: Track Start Times Desync (MEDIUM)

**Location:** `Audio/MixPlaybackEngine.cpp` lines 75-137, 627-643

**Problem:**
In `buildPlaybackState()`:
- `trackStartTimes` is populated for ALL mix tracks (indices 0..N-1)
- `trackSources` only contains successfully prepared tracks (skips failures)

In `mixActiveTracksForBlock()`, the loop uses index `i` from `trackSources` to access `trackStartTimes[i]`, causing index mismatch when any track fails to prepare.

**Fix:**
Store the track index alongside each source, or use a map keyed by trackId:

```cpp
struct PlaybackTrackSource {
    size_t originalIndex;  // Index into trackStartTimes
    // ... existing fields
};
```

Then in `mixActiveTracksForBlock()`:
```cpp
Duration_t trackStartMs = state->trackStartTimes[source->originalIndex];
```

---

### Issue 3: Data Race in BackgroundService (MEDIUM)

**Location:** `Database/BackgroundService.cpp` lines 49, 84-92

**Problem:**
The condition variable predicate reads `m_tasks.empty()` (line 92) while holding `m_conditionMutex`, but `m_tasks` is protected by `m_tasksMutex` (line 49). This is undefined behavior.

**Fix:**
Either:
1. Use the same mutex for both the condition variable and task list
2. Use an atomic flag to signal work availability
3. Lock `m_tasksMutex` inside the predicate (but beware of lock ordering)

Simplest fix - use atomic flag:
```cpp
std::atomic<bool> m_hasWork{false};

// In registerTask():
m_hasWork = true;
m_condition.notify_one();

// In predicate:
return m_shouldExit.load() || m_hasWork.load();
```

---

### Issue 4: SQLite Empty Vector UB (MEDIUM)

**Location:** `Database/Sqlite/SqliteStatement.cpp` line 128-130

**Problem:**
```cpp
const int rc = sqlite3_bind_blob(m_statement, m_param_index++, &blob[0], ...);
```
Accessing `&blob[0]` when `blob.empty()` is undefined behavior.

**Fix:**
```cpp
bool SqliteStatement::addParam(const std::vector<unsigned char> &blob)
{
    if (blob.empty())
    {
        return addNullParam();  // Or bind zero-length blob with nullptr
    }
    const int rc = sqlite3_bind_blob(m_statement, m_param_index++,
                                      blob.data(), (int)blob.size(), SQLITE_STATIC);
    // ...
}
```

---

### Issue 5: splitString Assumes Null-Termination (MEDIUM)

**Location:** `Utils/AssortedUtils.cpp` lines 71-123

**Problem:**
- Line 82-83: Iterates until null terminator, but `string_view` may not be null-terminated
- Line 114: `std::strchr(svseparators.data(), c)` expects null-terminated string

**Fix:**
Use proper `string_view` iteration:
```cpp
std::vector<std::string> splitString(std::string_view svtext, std::string_view svseparators, bool handle_quotation_marks)
{
    std::vector<std::string> result;
    size_t start = 0;

    for (size_t i = 0; i < svtext.size(); ++i)
    {
        char c = svtext[i];
        // Use svseparators.find(c) != npos instead of strchr
        if (svseparators.find(c) != std::string_view::npos)
        {
            // ... handle separator
        }
    }
    // ...
}
```

---

### Issue 6: Mp3QuickCheck Missing gcount() Check (LOW)

**Location:** `Database/BackgroundTasks/Mp3QuickCheck.cpp` lines 78-79, 149-150

**Problem:**
After `file.read()`, no check of `file.gcount()` to verify bytes actually read. Short files lead to parsing uninitialized buffer data.

**Fix:**
```cpp
file.read(buffer.data(), buffer.size());
if (file.gcount() < 4)  // Minimum needed for header check
{
    return std::nullopt;
}
```

---

### Issue 7: TrackScanner Memory Leak (LOW)

**Location:** `Database/TrackScanner.cpp` line 16, `Database/TrackScanner.h` lines 50, 67

**Problem:**
Scanners allocated with `new` but never deleted (default destructor).

**Fix:**
Use `std::unique_ptr`:
```cpp
// In header:
std::vector<std::unique_ptr<ITrackInfoScanner>> m_scanners;

// In constructor:
m_scanners.push_back(std::make_unique<scanners::Id3TagScanner>(m_db.getTagManager()));
```

---

### Issue 8: Audio Callback Allocations (LOW-MEDIUM)

**Location:** `Audio/MixPlaybackEngine.cpp` lines 672, 622-624

**Problem:**
- Line 672: `juce::AudioBuffer<float>` allocation in audio callback
- Lines 622+: Logging (even conditional) in audio callback

Both can cause real-time glitches.

**Fix:**
- Pre-allocate scratch buffers in `prepareToPlay()` and reuse them
- Remove all logging from audio callback, or use lock-free logging queue

```cpp
// In class:
juce::AudioBuffer<float> m_scratchBuffer;

// In prepareToPlay():
m_scratchBuffer.setSize(2, samplesPerBlockExpected);

// In audio callback:
m_scratchBuffer.setSize(numChannels, samplesToRead, false, false, true);  // No reallocation
```

---

## Priority Order

1. **Issue 1** - Use-after-free (crash risk)
2. **Issue 2** - Track timing desync (audible glitches)
3. **Issue 3** - Data race (potential crash/hang)
4. **Issue 4** - Empty vector UB (crash on edge case)
5. **Issue 5** - splitString UB (crash on edge case)
6. **Issue 8** - Audio allocations (glitches under load)
7. **Issue 6** - gcount check (wrong metadata on short files)
8. **Issue 7** - Memory leak (minor, scanners are long-lived)
