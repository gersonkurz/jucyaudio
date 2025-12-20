# JucyAudio - Issues & Code Review Findings

---

## Existing Feature Requests / Ideas

- If not all mixes can be shown because a folder is unmounted, show them hidden - if selected, instead of showing data, show a special screen "not available because XYZ is not mounted"
- Support being able to right-click on a file and play it in jucyaudio. Might have to add this file then to the database in case it does not exist yet?
- migrate from spdlog to quill - more logging, less performance impact.
- long-term task: support for folder reorganization needs to be improved

---

## Audio Performance Findings (December 2025)

Performance hotspots identified in the audio mixing system.

---

### Audio Mixing Hotspots

#### 1. Per-Sample getSample/setSample Calls (HIGH)

**Location:** `Audio/MixPlaybackEngine.cpp` - `mixActiveTracksForBlock()`

**Problem:**
Every sample operation calls `buffer.getSample()` and `buffer.setSample()` which include bounds checking and channel calculations. For a typical 512-sample block with 4 active tracks, that's 4,096 function calls per block.

**Fix:**
Use raw `float*` pointer access:
```cpp
float* leftOut = buffer.getWritePointer(0);
float* rightOut = buffer.getWritePointer(1);
const float* leftIn = sourceBuffer.getReadPointer(0);
const float* rightIn = sourceBuffer.getReadPointer(1);

for (int i = 0; i < numSamples; ++i)
{
    leftOut[i] += leftIn[i] * gain;
    rightOut[i] += rightIn[i] * gain;
}
```

---

#### 2. Per-Sample Envelope Calculation (MEDIUM-HIGH)

**Location:** `Audio/MixPlaybackEngine.cpp` - `getEnvelopeGainForTrack()`

**Problem:**
The envelope gain is recalculated for every single sample. Most of the time the gain is constant (1.0) or changing linearly. Per-sample calls add significant overhead.

**Fix:**
Calculate envelope at block boundaries or use envelope segments:
```cpp
// Calculate gain at start and end of block
float startGain = getEnvelopeGainForTrack(mixTrack, startTimeMs);
float endGain = getEnvelopeGainForTrack(mixTrack, endTimeMs);

// If same (within tolerance), apply constant gain
if (std::abs(startGain - endGain) < 0.001f)
{
    buffer.applyGain(startGain);
}
else
{
    // Linear interpolation for fades
    buffer.applyGainRamp(0, numSamples, startGain, endGain);
}
```

---

#### 3. setSize() Per Track (MEDIUM)

**Location:** `Audio/MixPlaybackEngine.cpp` - `mixActiveTracksForBlock()`

**Problem:**
`AudioBuffer::setSize()` is called for each track even with `keepExistingContent=false`. This involves checking capacity and potentially zeroing memory.

**Fix:**
Pre-allocate scratch buffer to maximum expected size in `prepareToPlay()`:
```cpp
// In prepareToPlay():
m_scratchBuffer.setSize(2, samplesPerBlockExpected * 2);  // Extra margin

// In audio callback - just clear, don't resize:
m_scratchBuffer.clear(0, numSamples);
```

---

#### 4. Per-Track Intersection Math (MEDIUM)

**Location:** `Audio/MixPlaybackEngine.cpp` - `mixActiveTracksForBlock()`

**Problem:**
For each track, intersection calculations determine if the track is active in the current block. This involves:
- Converting samples to milliseconds
- Calculating track end time (start + duration)
- Checking overlap with block range

With many tracks, this repeated math adds up.

**Fix:**
Pre-calculate track ranges in sample units during state construction:
```cpp
struct PlaybackTrackSource {
    juce::int64 startSample;  // Pre-converted from ms
    juce::int64 endSample;    // Pre-calculated once
    // ...
};
```

Then use simple integer comparisons in the audio callback.

---

#### 5. getNextAudioBlock() Overhead (LOW-MEDIUM)

**Location:** `Audio/MixPlaybackEngine.cpp` - per-track audio retrieval

**Problem:**
Each active track calls `resampler->getNextAudioBlock()` or `readerSource->getNextAudioBlock()`, which involves:
- Virtual function dispatch
- Internal state updates
- Potential disk I/O (if not pre-buffered)

**Mitigation:**
- Ensure sources are adequately pre-buffered
- Consider SIMD-optimized resampling
- Profile to verify this is actually a bottleneck (disk I/O usually dominates)

---

### Playback State Construction/Lookup

#### 6. O(n²) getTrackInfo Lookups (MEDIUM)

**Location:** `Audio/MixPlaybackEngine.cpp` - `buildPlaybackState()`

**Problem:**
In `buildPlaybackState()`, for each track (N tracks), `getTrackInfo(trackId)` performs a linear search through `trackInfos`. This is O(n²) complexity.

**Fix:**
Build a map during construction:
```cpp
PlaybackState* buildPlaybackState(MixProjectLoader* mixLoader)
{
    auto* state = new PlaybackState();
    state->trackInfos = mixLoader->getTrackInfos();  // Copy once

    // Build lookup map
    std::unordered_map<TrackId, const TrackInfo*> trackInfoMap;
    for (const auto& ti : state->trackInfos)
    {
        trackInfoMap[ti.trackId] = &ti;
    }

    // Now O(1) lookups
    for (const auto& mixTrack : mixLoader->getMixTracks())
    {
        auto it = trackInfoMap.find(mixTrack.trackId);
        if (it != trackInfoMap.end())
        {
            // Use it->second directly
        }
    }
}
```

---

#### 7. Repeated Duration Calculations (LOW)

**Location:** `Audio/MixPlaybackEngine.cpp` - track timing calculations

**Problem:**
Track duration calculations (e.g., getting duration from TrackInfo, converting to samples) may be repeated multiple times during state construction and during playback.

**Fix:**
Cache durations in sample units during state construction:
```cpp
struct PlaybackTrackSource {
    juce::int64 durationSamples;  // Cached at construction time
    // ...
};
```

---

## Priority Order

1. **Issue 1** - Per-sample getSample/setSample (biggest per-block overhead)
2. **Issue 2** - Per-sample envelope calculation (second biggest)
3. **Issue 6** - O(n²) lookups (affects loading time, not real-time but noticeable)
4. **Issue 4** - Per-track intersection math (can be pre-computed)
5. **Issue 3** - setSize per track (minor but easy fix)
6. **Issue 5** - getNextAudioBlock overhead (profile first)
7. **Issue 7** - Repeated duration calcs (minor optimization)

---

## Config File Performance (December 2025)

### Config Written 84 Times on Startup (MEDIUM)

**Location:** `Config/toml_backend.h` - `setValueAtPath()` calls `saveToFile()` after every value change

**Problem:**
During startup, the config file is written 84 times because:
1. Three separate `TomlBackend` instances are created
2. Instance #3 (in `DynamicColumnManager.cpp`) saves ALL settings when initializing default columns
3. Each `TypedValue::save()` triggers a full file write

**Root Cause:**
- `DynamicColumnManager::getColumnsForNode()` calls `config::theSettings.save(backend)` which saves all 84 settings
- Each setting's `save()` calls `setValueAtPath()` which calls `saveToFile()`

**Potential Fixes:**
1. **Batch mode API**: Add `beginBatch()`/`endBatch()` to `TomlBackend` that defers writes until batch ends
2. **Dirty flag with explicit flush**: Mark dirty on change, only write on explicit `flush()` or destructor
3. **Surgical fix**: Change `DynamicColumnManager` to only save the specific column vector, not all settings

**Considerations:**
- Batch mode in destructor risks losing changes on crash for long-lived backends
- Explicit batch API is safest - opt-in for bulk operations, immediate writes for normal usage
- Current behavior is safe but slow (84 file writes in ~50ms during startup)
