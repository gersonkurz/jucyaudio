# JucyAudio - Mix Refcounting Architecture (Thread-Safe Playback)

## Status: DESIGN DOCUMENT - IMPLEMENTATION IN PROGRESS

**Created:** 2025-10-22
**Version:** 1.1 - SIMPLIFIED APPROACH
**Author:** Architecture redesign for thread-safe mix playback

**IMPORTANT DESIGN CHANGE (v1.1):**
After initial implementation attempt, we discovered a much simpler solution:
- **DO NOT make TrackInfo refcounted** - it remains a regular copyable struct
- **DO make PlaybackState (the container) refcounted** - holds all playback data
- TrackInfo remains copyable, minimizing changes to 90% of the codebase
- Only audio playback code needs modification
- Same thread-safety guarantees with far less complexity

---

## Problem Statement

### Current Critical Issue

The application experiences access violation crashes in `MixPlaybackEngine::mixActiveTracksForBlock()` when the audio thread reads mix data while the UI thread modifies it. The crash occurs at line 484:

```cpp
const auto trackDurationSamples = static_cast<juce::int64>(
    (source->trackInfo->duration.count() / 1000.0) * m_sampleRate);  // CRASH HERE
```

### Root Cause Analysis

**Three threads interact with mix data:**

1. **UI Thread**: Modifies mix (add/delete tracks, change cue points, envelopes, gain)
2. **Audio Callback Thread**: Real-time thread reading mix data for playback (highest priority)
3. **Background Threads**: Waveform loading, BPM analysis (not directly relevant)

**The Problem:**

```cpp
// PlaybackTrackSource stores RAW POINTERS
struct PlaybackTrackSource
{
    const TrackInfo *trackInfo;   // DANGER: Points into MixProjectLoader's vector
    const MixTrack *mixTrack;     // DANGER: Points into MixProjectLoader's vector
};

// MixProjectLoader owns the data via vectors
class MixProjectLoader
{
    std::vector<TrackInfo> m_trackInfos;              // Owned data
    std::unordered_map<TrackId, const TrackInfo*> m_trackInfosMap;  // Pointers into vector
};
```

**When UI modifies mix:**
```cpp
// User deletes a track
mixManager.removeTrackFromMix(mixId, trackId);
mixLoader.reloadFromDatabase();  // Replaces m_trackInfos vector

// Result: ALL pointers in PlaybackTrackSource are now DANGLING
// Audio thread crashes when it tries to read them
```

### Why Current Locking Doesn't Work

1. **No lock in audio callback**: `MixPlaybackEngine::getNextAudioBlock()` doesn't lock (can't - would cause dropouts)
2. **Inconsistent lock usage**: Some UI operations use `withMixEngineLock()`, others don't
3. **Priority inversion risk**: If audio thread held locks, it could be blocked by lower-priority UI thread
4. **Real-time constraint violation**: Audio callbacks must complete in microseconds, locks can take milliseconds

---

## Architecture Overview

### Solution: Refcount the Playback Snapshot, Not Individual Objects

**Use the proven `IRefCounted` system already used for navigation nodes:**

```cpp
// Navigation nodes use this successfully
struct INavigationNode : public IRefCounted
{
    virtual void retain(REFCOUNT_DEBUG_SPEC) const = 0;
    virtual void release(REFCOUNT_DEBUG_SPEC) const = 0;
};
```

**Apply the same pattern to the playback state CONTAINER:**

```cpp
// PlaybackState holds ALL data needed for rendering
struct PlaybackState : public RefCountImpl
{
    std::vector<TrackInfo> trackInfos;  // OWNED by value - TrackInfo remains copyable!
    std::vector<std::unique_ptr<PlaybackTrackSource>> trackSources;
    std::vector<Duration_t> trackStartTimes;
    Duration_t totalDuration{0};

    // PlaybackTrackSource can safely hold pointers into trackInfos
    // because PlaybackState is refcounted
};

// TrackInfo remains a regular struct - NO CHANGES!
struct TrackInfo
{
    TrackId trackId;
    std::string filename;
    Duration_t duration;
    // ... all existing fields - COPYABLE as before!
};
```

**Key Insight: Audio thread holds ONE reference to PlaybackState (via atomic pointer read). PlaybackState owns all data, so pointers into that data remain valid. No retain/release in audio callback - just atomic pointer load.**

---

## Design Principles

### Why Refcount the Container, Not Individual Objects?

**Original Idea:** Make TrackInfo refcounted
**Problem:** TrackInfo's deleted copy constructor breaks 90% of the codebase that uses `std::vector<TrackInfo>`

**Better Idea:** Make PlaybackState (the container) refcounted
**Benefits:**
- ✅ TrackInfo remains copyable - no changes to database layer, UI code, etc.
- ✅ Only audio playback code needs modification
- ✅ PlaybackState owns copies of TrackInfo data, so pointers into that data are safe
- ✅ Simpler mental model: "snapshot refcounting" not "per-object refcounting"
- ✅ Same thread-safety guarantees

### The Atomic Pointer Pattern: No Retain/Release in Audio Thread

**Critical for real-time safety:**

```cpp
// Audio thread - NO retain/release calls!
void getNextAudioBlock(...)
{
    auto* state = m_currentPlaybackState.load();  // Atomic read - just a pointer copy
    if (!state) return;

    // Use state->trackInfos, state->trackSources, etc.
    // Safe because m_currentPlaybackState "holds" the reference
}

// UI thread - Atomic swap + deferred deletion
void loadMix(...)
{
    auto* newState = buildNewPlaybackState();  // refcount=1
    auto* oldState = m_currentPlaybackState.exchange(newState);  // Atomic swap

    // Queue old state for deletion on message thread
    if (oldState)
    {
        m_statesToDelete.push_back(oldState);
        juce::MessageManager::callAsync([this]() { cleanupOldStates(); });
    }
}

// Message thread - Safe to call release() and trigger deletion
void cleanupOldStates()
{
    for (auto* state : m_statesToDelete)
        state->release();  // May delete, but we're on message thread
    m_statesToDelete.clear();
}
```

**Key insight:** The atomic pointer `m_currentPlaybackState` "owns" one reference. Audio thread just reads it. No retain/release in hot path.

---

## Detailed Implementation (Simplified Approach)

### Phase 1: Create PlaybackState Container

**File: `Audio/MixPlaybackEngine.h`**

**NEW struct:**
```cpp
namespace jucyaudio::audio
{
    // Forward declare
    struct PlaybackTrackSource;

    /**
     * @brief Refcounted container holding all data needed for mix playback.
     *
     * This struct owns copies of TrackInfo data and the PlaybackTrackSource objects.
     * The audio thread holds a reference to PlaybackState (via atomic pointer), making
     * it safe for PlaybackTrackSource to hold pointers into trackInfos.
     *
     * Thread-safety: The atomic pointer in MixPlaybackEngine ensures only one thread
     * can modify the pointer at a time. The audio thread only reads.
     */
    struct PlaybackState : public database::RefCountImpl
    {
        std::vector<database::TrackInfo> trackInfos;  // Owned copies
        std::vector<std::unique_ptr<PlaybackTrackSource>> trackSources;
        std::vector<Duration_t> trackStartTimes;
        Duration_t totalDuration{0};

        // Helper to find TrackInfo by ID (returns pointer into trackInfos vector)
        const database::TrackInfo* getTrackInfo(database::TrackId trackId) const
        {
            auto it = std::find_if(trackInfos.begin(), trackInfos.end(),
                [trackId](const auto& ti) { return ti.trackId == trackId; });
            return (it != trackInfos.end()) ? &(*it) : nullptr;
        }
    };
}
```

**Impact:**
- ✅ TrackInfo remains unchanged - copyable struct
- ✅ PlaybackState is refcounted container
- ✅ Audio thread can safely hold pointers into PlaybackState->trackInfos

---

### Phase 2: Update PlaybackTrackSource to Hold Safe Pointers

**File: `Audio/MixPlaybackEngine.h`**

**BEFORE (Dangling Pointers):**
```cpp
struct PlaybackTrackSource
{
    TrackId trackId;
    const TrackInfo *trackInfo;      // DANGER: Points into MixProjectLoader
    const MixTrack *mixTrack;        // DANGER: Points into MixProjectLoader

    std::unique_ptr<juce::AudioFormatReader> reader;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::unique_ptr<juce::ResamplingAudioSource> resampler;

    PlaybackTrackSource(TrackId id, const TrackInfo *ti, const MixTrack *mt)
        : trackId(id), trackInfo(ti), mixTrack(mt)
    {}
};
```

**AFTER (Safe Pointers into PlaybackState):**
```cpp
struct PlaybackTrackSource
{
    TrackId trackId;
    const TrackInfo* trackInfo;  // SAFE: Points into PlaybackState->trackInfos
    MixTrack mixTrack;           // OWNED copy (POD data)

    std::unique_ptr<juce::AudioFormatReader> reader;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::unique_ptr<juce::ResamplingAudioSource> resampler;

    PlaybackTrackSource(TrackId id, const TrackInfo* ti, const MixTrack& mt)
        : trackId(id)
        , trackInfo(ti)
        , mixTrack(mt)  // Copy MixTrack (small POD struct)
    {}

    bool prepare(juce::AudioFormatManager& formatManager, double targetSampleRate, int blockSize);
    juce::AudioSource* getAudioSource();
};
```

**Key Changes:**
1. `trackInfo`: Still a pointer, but now points into `PlaybackState->trackInfos` (safe because PlaybackState is refcounted)
2. `mixTrack`: Changed from pointer to owned copy (MixTrack is small POD)
3. No manual retain/release needed - PlaybackState owns the lifetime

---

### Phase 4: Update MixPlaybackEngine for Lock-Free Audio

**File: `Audio/MixPlaybackEngine.cpp`**

**`loadMix()` - Setup Phase (UI Thread, Can Lock):**
```cpp
bool MixPlaybackEngine::loadMix(MixProjectLoader* mixLoader)
{
    // Clear any old sources that are safe to delete now
    clearOldTrackSources();

    // 1. Construct new track sources (off the audio thread's critical path)
    // This involves copying MixTrack data and retaining TrackInfo references
    auto* newSources = createTrackSources(mixLoader); // Helper to build the vector

    if (!newSources)
    {
        // If creation failed, ensure we don't leave a dangling pointer
        m_atomicTrackSources.store(nullptr);
        return false;
    }

    // 2. Atomically swap the new sources with the currently active ones
    // The old sources are now "retired" and will be cleaned up later
    std::vector<std::unique_ptr<PlaybackTrackSource>>* oldSources = m_atomicTrackSources.exchange(newSources);

    // 3. Add the old sources to a list for deferred deletion
    if (oldSources)
    {
        m_oldTrackSourcesToClear.push_back(oldSources);
    }

    m_mixLoader = mixLoader; // Update mix loader reference

std::vector<std::unique_ptr<PlaybackTrackSource>>* MixPlaybackEngine::createTrackSources(MixProjectLoader* mixLoader)
{
    if (!mixLoader)
        return nullptr;

    auto* newSources = new std::vector<std::unique_ptr<PlaybackTrackSource>>();
    const auto& mixTracks = mixLoader->getMixTracks();
    newSources->reserve(mixTracks.size());

    for (size_t i = 0; i < mixTracks.size(); ++i)
    {
        const auto& mixTrack = mixTracks[i];
        // Get RETAINED pointer from MixProjectLoader's in-memory cache
        TrackInfo* trackInfo = mixLoader->getRetainedTrackInfo(mixTrack.trackId);

        if (!trackInfo)
            continue;

        // Create source - constructor RETAINS the TrackInfo
        auto source = std::make_unique<PlaybackTrackSource>(
            mixTrack.trackId,
            trackInfo,   // Will be retained by PlaybackTrackSource constructor
            mixTrack     // Pass by const ref - will be copied
        );

        if (source->prepare(m_formatManager, m_sampleRate, m_blockSize))
        {
            newSources->push_back(std::move(source));
        }
        else
        {
            // If preparation fails, release the retained TrackInfo
            trackInfo->release(REFCOUNT_DEBUG_ARGS);
        }
    }
    return newSources;
}
```

**`mixActiveTracksForBlock()` - Audio Thread (NO LOCKS!):**
```cpp
void MixPlaybackEngine::mixActiveTracksForBlock(
    juce::AudioBuffer<float>& buffer,
    juce::int64 startSample,
    int numSamples)
{
    // NO LOCK - we hold retained references, safe to read!
    // Get the current active track sources (atomic read)
    auto* currentTrackSources = m_atomicTrackSources.load();
    if (!currentTrackSources)
    {
        buffer.clearActiveBufferRegion();
        return;
    }

    const int numChannels = buffer.getNumChannels();

    for (size_t i = 0; i < currentTrackSources->size(); ++i)
    {
        auto& source = (*currentTrackSources)[i];

        // Validate pointers
        if (!source || !source->trackInfo || !source->reader)
            continue;

        // Safe access - trackInfo is retained, cannot be deleted
        const auto& mixTrack = source->mixTrack;  // Owned copy, always valid
        const auto duration = source->trackInfo->duration;  // SAFE!
        const auto sampleRate = source->trackInfo->sample_rate;  // SAFE!

        if (i >= m_trackStartTimes.size())
            continue;

        const auto trackStartSamples = static_cast<juce::int64>(
            (m_trackStartTimes[i].count() / 1000.0) * m_sampleRate
        );

        const auto trackDurationSamples = static_cast<juce::int64>(
            (duration.count() / 1000.0) * m_sampleRate  // NO CRASH!
        );

        // ... rest of rendering logic
    }
}
```

**Key Points:**
- ✅ No `juce::ScopedLock` in audio callback
- ✅ Access through `source->trackInfo->duration` is SAFE (retained reference)
- ✅ Access through `source->mixTrack` is SAFE (owned copy)
- ✅ UI thread can reload MixProjectLoader without crashing audio thread
- ✅ Playback data is updated via atomic swap, minimizing audio thread blocking
- ✅ Old playback data is safely deleted after a grace period

---

### Phase 5: Remove All withMixEngineLock() Usage

**File: `UI/MixEditorComponent.cpp`**

**BEFORE (Locks Everywhere):**
```cpp
void MixEditorComponent::updateEnvelopeInData(int orderInMix, const std::vector<EnvelopePoint>& points)
{
    // OLD: Used locking to protect against audio thread
    if (m_playbackController)
    {
        m_playbackController->withMixEngineLock([&]()
        {
            auto& mixTracks = mixLoader.getMixTracks();
            for (auto& track : mixTracks)
            {
                if (track.orderInMix == orderInMix)
                {
                    track.envelopePoints = points;
                    saveMixChanges();
                    break;
                }
            }
        });
    }
}
```

**AFTER (No Locks Needed!):**
```cpp
void MixEditorComponent::updateEnvelopeInData(int orderInMix, const std::vector<EnvelopePoint>& points)
{
    // NEW: No locks needed - audio thread has its own retained references

    auto& mixLoader = m_node->getMixProjectLoader();
    auto& mixTracks = mixLoader.getMixTracks();

    for (auto& track : mixTracks)
    {
        if (track.orderInMix == orderInMix)
        {
            track.envelopePoints = points;
            saveMixChanges();  // Saves to database
            break;
        }
    }

    // Reload mix - triggers atomic swap in MixPlaybackEngine to update playback data
    if (m_playbackController && m_playbackController->isMixMode())
    {
        m_playbackController->loadMix(&mixLoader);
    }
}
```

**Same pattern for ALL UI modifications:**
- `handleDeleteSelectedTrack()`
- `handlePasteTracks()`
- `updateCueAttachInData()`
- `updateGainAdjustmentInData()`
- Undo/Redo operations

**Delete `withMixEngineLock()` entirely:**
```cpp
// File: UI/PlaybackController.h and .cpp
// DELETE THIS METHOD:
// void PlaybackController::withMixEngineLock(std::function<void()> action)

// File: Audio/MixPlaybackEngine.h
// DELETE THIS MEMBER AND ALL REFERENCES TO IT:
// mutable juce::CriticalSection m_critSec;
```

---

### Phase 6: Database Layer Returns Refcounted Objects

**File: `Database/TrackLibrary.h` and `.cpp`**

**NEW METHODS:**
```cpp
class TrackLibrary
{
public:
    // Returns vector of RETAINED TrackInfo objects
    // Caller must release each pointer when done
    std::vector<TrackInfo*> getTrackInfosForMix(MixId mixId);

    // Returns vector of RETAINED TrackInfo objects for tracks
    std::vector<TrackInfo*> getTrackInfosForTracks(const std::vector<TrackId>& trackIds);

    // Get a single RETAINED TrackInfo (refcount=1)
    // Caller must release when done
    TrackInfo* getTrackInfo(TrackId trackId);

private:
    // Helper to load from database
    TrackInfo* loadTrackInfoFromDatabase(TrackId trackId);
};
```

**Implementation:**
```cpp
std::vector<TrackInfo*> TrackLibrary::getTrackInfosForMix(MixId mixId)
{
    std::vector<TrackInfo*> result;

    // Get track IDs for this mix
    auto trackIds = m_trackDatabase->getTrackIdsForMix(mixId);

    for (auto trackId : trackIds)
    {
        // Each call returns refcount=1 object
        auto* trackInfo = getTrackInfo(trackId);
        if (trackInfo)
            result.push_back(trackInfo);
    }

    return result;  // Caller owns all references
}

TrackInfo* TrackLibrary::getTrackInfo(TrackId trackId)
{
    // Load from database
    auto* trackInfo = loadTrackInfoFromDatabase(trackId);
    // trackInfo now has refcount=1

    return trackInfo;  // Caller owns the reference
}

TrackInfo* TrackLibrary::loadTrackInfoFromDatabase(TrackId trackId)
{
    // Create new refcounted object (refcount=1 by default)
    auto* trackInfo = new TrackInfo();

    // Load data from SQLite
    auto stmt = /* prepare SELECT query */;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        trackInfo->trackId = sqlite3_column_int64(stmt, 0);
        trackInfo->filename = /* ... */;
        trackInfo->duration = /* ... */;
        // ... load all fields
    }

    return trackInfo;  // Caller owns refcount=1
}
```

---

### Phase 6.5: Optional Database Pooling (Future Optimization)

**NOT REQUIRED for thread safety, but enables efficiency:**

```cpp
class TrackLibrary
{
private:
    // Cache of refcounted TrackInfo objects
    std::unordered_map<TrackId, TrackInfo*> m_trackInfoCache;
    std::mutex m_cacheMutex;  // Protects cache map only (not TrackInfo contents!)

public:
    TrackInfo* getTrackInfo(TrackId trackId)
    {
        {
            std::lock_guard lock(m_cacheMutex);

            auto it = m_trackInfoCache.find(trackId);
            if (it != m_trackInfoCache.end())
            {
                // Found in cache - retain and return
                it->second->retain(REFCOUNT_DEBUG_ARGS);
                return it->second;  // Caller gets refcount+1
            }
        }

        // Not in cache - load from database
        auto* trackInfo = loadTrackInfoFromDatabase(trackId);  // refcount=1

        {
            std::lock_guard lock(m_cacheMutex);

            // Add to cache
            m_trackInfoCache[trackId] = trackInfo;
            trackInfo->retain(REFCOUNT_DEBUG_ARGS);  // One for cache
            trackInfo->retain(REFCOUNT_DEBUG_ARGS);  // One for caller
        }

        return trackInfo;
    }

    // Periodic cleanup (call from background thread)
    void pruneCache()
    {
        std::lock_guard lock(m_cacheMutex);

        // Remove entries where refcount=1 (only cache holds reference)
        std::erase_if(m_trackInfoCache, [](const auto& pair) {
            // Note: This is tricky - need to check refcount without racing
            // Safe way: just remove based on heuristics (age, size)
            // Or: Don't prune at all - cache grows but stays bounded by library size
            return false;  // For now, don't prune
        });
    }
};
```

**Benefits:**
- Same TrackInfo object shared across mix editor, library view, working sets
- Reduces memory usage
- Improves cache locality
- Database only queried once per unique track

---

## Code Impact Analysis

### Files Requiring Changes

**Phase 1: TrackInfo Refcounting**
- ✏️ `Database/Includes/TrackInfo.h` - Add `: public RefCountImpl`
- ✏️ `Database/TrackLibrary.h` - New methods returning `TrackInfo*`
- ✏️ `Database/TrackLibrary.cpp` - Implement refcounted returns

**Phase 2: MixProjectLoader**
- ✏️ `Audio/MixProjectLoader.h` - Change vectors to pointers
- ✏️ `Audio/MixProjectLoader.cpp` - Add retain/release in lifecycle
- ✏️ `Database/Nodes/MixNode.h` - Update if it accesses TrackInfo
- ✏️ `Database/Nodes/MixNode.cpp` - Update if it accesses TrackInfo

**Phase 3: PlaybackTrackSource**
- ✏️ `Audio/MixPlaybackEngine.h` - Update PlaybackTrackSource struct
- ✏️ `Audio/MixPlaybackEngine.cpp` - Add retain/release, remove locks

**Phase 4: UI Components**
- ✏️ `UI/MixEditorComponent.cpp` - Remove all `withMixEngineLock()` calls
- ✏️ `UI/PlaybackController.h` - Delete `withMixEngineLock()` method
- ✏️ `UI/PlaybackController.cpp` - Delete `withMixEngineLock()` implementation

**Phase 5: Database Layer**
- ✏️ `Database/Sqlite/SqliteTrackDatabase.h` - Return refcounted objects
- ✏️ `Database/Sqlite/SqliteTrackDatabase.cpp` - Implement refcounted returns

### Compilation Dependencies

**Must be updated together (atomic change):**
1. TrackInfo definition
2. MixProjectLoader storage
3. PlaybackTrackSource constructor
4. Database return types

**Cannot be done piecemeal** - the refcounting change is all-or-nothing for TrackInfo.

---

## Testing Strategy

### Unit Tests

**Test 1: Refcounting Basics**
```cpp
TEST(TrackInfoRefcounting, BasicRetainRelease)
{
    auto* ti = new TrackInfo();
    // refcount = 1

    ti->retain(REFCOUNT_DEBUG_ARGS);
    // refcount = 2

    ti->release(REFCOUNT_DEBUG_ARGS);
    // refcount = 1

    ti->release(REFCOUNT_DEBUG_ARGS);
    // refcount = 0, deleted
}
```

**Test 2: Ownership Transfer**
```cpp
TEST(MixProjectLoader, OwnsTrackInfos)
{
    MixProjectLoader loader;
    loader.loadMix(testMixId);

    // Loader owns references
    auto* ti = loader.getTrackInfoForId(testTrackId);
    ASSERT_NE(ti, nullptr);

    // Get refcount (if we add a getter for testing)
    // EXPECT_EQ(ti->getRefCount(), 1);
}
```

### Integration Tests

**Test 3: Delete Track During Playback**
```cpp
TEST(MixPlayback, DeleteTrackWhilePlaying)
{
    // Load and play mix
    playbackController.loadMix(&mixLoader);
    playbackController.play();

    // Audio thread now holds retained references

    // Delete a track from UI thread
    mixLoader.removeTrack(trackId);
    mixLoader.reloadFromDatabase();

    // Sleep to let audio thread render some blocks
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Should NOT crash

    playbackController.stop();
}
```

**Test 4: Rapid Undo/Redo During Playback**
```cpp
TEST(MixPlayback, RapidUndoRedoWhilePlaying)
{
    playbackController.loadMix(&mixLoader);
    playbackController.play();

    // Spam undo/redo
    for (int i = 0; i < 100; ++i)
    {
        if (theUndoManager.canUndo(mixId))
            theUndoManager.undo(mixId);
        if (theUndoManager.canRedo(mixId))
            theUndoManager.redo(mixId);
    }

    // Should NOT crash
    playbackController.stop();
}
```

### Stress Tests

**Test 5: Memory Leak Detection**
```cpp
TEST(MixPlayback, NoMemoryLeaks)
{
    // Load and unload mix many times
    for (int i = 0; i < 1000; ++i)
    {
        mixLoader.loadMix(testMixId);
        playbackController.loadMix(&mixLoader);
        playbackController.play();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        playbackController.stop();
        mixLoader.unloadMix();
    }

    // Check memory usage hasn't grown
    // (Requires memory profiler or refcount leak detection)
}
```

**Test 6: Concurrent Access**
```cpp
TEST(MixPlayback, ConcurrentModifications)
{
    playbackController.loadMix(&mixLoader);
    playbackController.play();

    // Launch multiple threads doing modifications
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i)
    {
        threads.emplace_back([&]() {
            for (int j = 0; j < 100; ++j)
            {
                // Random modifications
                mixLoader.updateTrackEnvelope(/*...*/);
                mixLoader.reloadFromDatabase();
            }
        });
    }

    for (auto& t : threads)
        t.join();

    // Should NOT crash
    playbackController.stop();
}
```

---

## Migration Path

### Step-by-Step Implementation Order

**Step 1: Enable Refcount Debugging** (5 minutes)
```cpp
// Database/Includes/IRefCounted.h
#define USE_REFCOUNT_DEBUGGING  // Uncomment this line
```

**Step 2: Make TrackInfo Refcounted** (30 minutes)
- ✏️ Update TrackInfo.h
- ✏️ Compile and fix any immediate errors
- ⚠️ At this point, nothing is retained/released - all refcounts will be 1

**Step 3: Update Database Layer** (2 hours)
- ✏️ TrackLibrary returns `TrackInfo*` with refcount=1
- ✏️ Update all database query methods
- ✅ Unit test: Verify returned objects have refcount=1

**Step 4: Update MixProjectLoader** (2 hours)
- ✏️ Change m_trackInfos to `std::vector<TrackInfo*>`
- ✏️ Add retain/release in constructor/destructor
- ✏️ Update loadMix() to handle refcounting
- ✅ Unit test: Verify loader owns references correctly

**Step 5: Update PlaybackTrackSource** (3 hours)
- ✏️ Change trackInfo to retained pointer
- ✏️ Change mixTrack from pointer to copy
- ✏️ Add retain in constructor, release in destructor
- ✅ Unit test: Verify source holds retained reference

**Step 6: Update MixPlaybackEngine** (3 hours)
- ✏️ Remove `m_critSec` and `getLock()`
- ✏️ Change `m_trackSources` to `std::atomic<std::vector<std::unique_ptr<PlaybackTrackSource>>*>`
- ✏️ Implement `createTrackSources()` to build new playback state
- ✏️ Update `loadMix()` to use atomic swap and deferred deletion
- ✏️ Update `unloadMixInternal()` to handle atomic sources and deferred deletion
- ✏️ Update `mixActiveTracksForBlock()` to read from atomic sources
- ✏️ Implement `clearOldTrackSources()` for deferred deletion
- ✅ Integration test: Play mix, should not crash

**Step 7: Update MixEditorComponent** (2 hours)
- ✏️ Remove all `withMixEngineLock()` calls
- ✏️ Call `loadMix()` after each modification to trigger playback state update
- ✏️ Call `clearOldTrackSources()` periodically (e.g., on a timer or after a few UI updates) to manage deferred deletion
- ✅ Integration test: Delete track during playback

**Step 8: Remove Lock Infrastructure** (30 minutes)
- ✏️ Delete `withMixEngineLock()` from PlaybackController
- ✏️ Delete `m_critSec` member from MixPlaybackEngine and all its usages
- ✏️ Remove unused `#include <mutex>` if any

**Step 9: Comprehensive Testing** (4 hours)
- ✅ Run all stress tests
- ✅ Memory leak detection with valgrind/ASAN
- ✅ Manual testing: Delete tracks, undo/redo, rapid edits during playback

**Step 10: Code Review & Documentation** (2 hours)
- 📝 Update code comments
- 📝 Update CLAUDE.md with new architecture
- 📝 Mark this document as IMPLEMENTED

**Total Estimated Time: 18-20 hours**

---

## Performance Implications

### Why This Is Fast

**Compared to Snapshot/Copy Approach:**

| Operation | Refcounting (TrackInfo) | Copying (MixTrack) | Atomic Swap (PlaybackEngine) |
|-----------|-------------------------|--------------------|------------------------------|
| **Load Mix** | Atomic increment per TrackInfo | Copy MixTrack data | Atomic pointer exchange |
| **Delete Track** | Atomic decrement | Copy MixTrack data | Atomic pointer exchange |
| **Change Envelope** | Atomic inc/dec | Copy MixTrack data | Atomic pointer exchange |
| **Undo** | Atomic inc/dec | Copy MixTrack data | Atomic pointer exchange |
| **Memory Usage** | One TrackInfo per unique track | One MixTrack copy per PlaybackTrackSource | Minimal overhead for pointer |

**Example: 100-track mix, 10 edits:**
- Refcounting (TrackInfo): 100 objects created, 1000 atomic operations
- Copying (MixTrack): 1000 MixTrack copies (100 x 10 edits)
- Atomic Swap: Minimal CPU cycles for pointer exchange
- Snapshot (shared_ptr): 1000 objects created (100 × 10), 1000 vector allocations

**Cache Performance:**
- Refcounting (TrackInfo): Objects stay at same memory address
- Atomic Swap: New playback state is constructed off the audio thread, then atomically swapped. The audio thread always accesses a consistent, contiguous block of memory.
- Snapshot (shared_ptr): New objects → new addresses → cache misses

### Memory Overhead

**Per TrackInfo Object:**
```
Refcounting:  sizeof(TrackInfo) + sizeof(atomic<int>) = ~256 bytes + 4 bytes
Snapshot:     sizeof(TrackInfo) × N_snapshots = ~256 bytes × N
```

For a 100-track mix with 3 simultaneous snapshots (old, current, loading):
- Refcounting: 26KB
- Snapshot: 78KB (3× more)

---

## Debugging Support

### REFCOUNT_DEBUG_ARGS

**Enable in Debug Builds:**
```cpp
// Database/Includes/IRefCounted.h
#define USE_REFCOUNT_DEBUGGING  // Uncomment for debug builds
```

**When Enabled:**
```cpp
trackInfo->retain(__FILE__, __LINE__);  // Records file:line of retain
trackInfo->release(__FILE__, __LINE__); // Records file:line of release
```

**Leak Detection:**
If an object is leaked (refcount never reaches zero), the last retain location is logged:
```
[ERROR] TrackInfo leaked! trackId=12345
  Last retain: MixEditorComponent.cpp:567
  Last release: PlaybackController.cpp:234
  Final refcount: 1
```

### Visual Studio Debugger

**Watch Window:**
```
trackInfo->m_refCount._My_val  // View current refcount
```

**Conditional Breakpoint:**
```cpp
// Break when refcount becomes invalid
trackInfo->m_refCount._My_val < 0 || trackInfo->m_refCount._My_val > 100
```

---

## Comparison with Navigation Nodes

### Proven Pattern

**Navigation nodes already use this successfully:**

```cpp
// Navigation tree retains/releases nodes
INavigationNode* node = folderNode->get("subfolder");  // Returns retained
node->retain(REFCOUNT_DEBUG_ARGS);  // Now held by UI
// ... use node ...
node->release(REFCOUNT_DEBUG_ARGS);  // Release when done
```

**Same pattern for TrackInfo:**

```cpp
// Mix loader holds TrackInfo
TrackInfo* trackInfo = mixLoader.getTrackInfoForId(trackId);  // Borrowed
trackInfo->retain(REFCOUNT_DEBUG_ARGS);  // Audio engine retains
// ... audio thread uses trackInfo ...
trackInfo->release(REFCOUNT_DEBUG_ARGS);  // Release when done
```

**Lessons Learned from Navigation Nodes:**
1. ✅ Refcounting works great for complex object graphs
2. ✅ REFCOUNT_DEBUG_ARGS catches leaks early
3. ✅ `EnsureNodeIsReleased` helper class simplifies exception safety
4. ✅ Atomic operations fast enough for frequent retain/release

---

## Future Enhancements

### Phase 7: Database Pooling (Optional)

**Benefits:**
- Reuse TrackInfo objects across multiple subsystems
- Mix editor, library view, working sets all share same objects
- Reduces database queries
- Improves cache locality

**Implementation:**
```cpp
// TrackLibrary maintains pool
std::unordered_map<TrackId, TrackInfo*> m_trackInfoPool;

TrackInfo* getTrackInfo(TrackId trackId)
{
    auto it = m_trackInfoPool.find(trackId);
    if (it != m_trackInfoPool.end())
    {
        it->second->retain(REFCOUNT_DEBUG_ARGS);
        return it->second;
    }

    auto* ti = loadFromDatabase(trackId);
    m_trackInfoPool[trackId] = ti;
    ti->retain(REFCOUNT_DEBUG_ARGS);  // One for pool
    ti->retain(REFCOUNT_DEBUG_ARGS);  // One for caller
    return ti;
}
```

### Phase 8: Make MixTrack Refcounted (If Needed)

**Currently: MixTrack is copied (it's POD)**
- Simple, safe, fast enough (small struct)

**If MixTrack becomes large (e.g., adds complex fields):**
- Apply same refcounting pattern
- PlaybackTrackSource stores `MixTrack*` instead of copy

---

## Rollback Plan

**If implementation fails, revert in reverse order:**

1. Revert MixEditorComponent changes (restore `withMixEngineLock()` calls)
2. Revert MixPlaybackEngine changes (restore old pointers)
3. Revert PlaybackTrackSource changes
4. Revert MixProjectLoader changes
5. Revert TrackInfo changes (remove refcounting)
6. Revert database changes

**Git Strategy:**
- Commit each phase separately
- Tag working states: `mixrc-phase-1`, `mixrc-phase-2`, etc.
- If crash/regression detected, revert to last working tag

---

## Success Criteria

### Must Have (Required for Merge)

✅ **No crashes during playback with concurrent edits**
✅ **All existing tests pass**
✅ **No memory leaks (verified with ASAN/valgrind)**
✅ **No audio dropouts (real-time constraint maintained)**
✅ **Refcount debugging shows no leaks**

### Nice to Have (Future Improvements)

🔲 Database pooling implemented
🔲 Performance benchmarks show improvement
🔲 Reduced memory usage (profiler confirms)

---

## Conclusion

This refcounting architecture leverages existing, proven infrastructure (IRefCounted) to solve the threading issues without introducing complex snapshot systems or performance-killing locks in the audio thread.

**Key Advantages:**
1. ✅ Thread-safe by design (no dangling pointers possible)
2. ✅ Lock-free audio rendering (real-time safe)
3. ✅ Minimal code changes (reuse existing patterns)
4. ✅ Zero performance cost (atomic operations only)
5. ✅ Built-in leak detection (REFCOUNT_DEBUG_ARGS)
6. ✅ Fits "80s oldschool" philosophy (explicit, predictable)

**This is the right solution for JucyAudio.**
