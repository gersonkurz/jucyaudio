# Waveform Loading Task Implementation Plan

## Overview
Transform the current asynchronous waveform loading in the Mix Editor into a managed, cancellable task with progress indication. This will provide users with feedback while waveforms load and prevent interaction with incomplete mixes.

## Current Implementation Analysis

### How Waveforms Currently Load:
1. **MixEditorComponent::loadMix()** is called when a mix is selected
2. Depending on `useVirtualTimeline` setting:
   - **VirtualTimelineComponent::loadMixProject()** - New virtual timeline (Phase 3)
   - **TimelineComponent::populateFrom()** - Original timeline implementation
3. For each track in the mix:
   - Check database cache for serialized waveform data (`loadWaveform`)
   - If cached: Load from database blob directly
   - If not cached: Set audio file as source, triggering async loading
4. When async loading completes:
   - `changeListenerCallback` is triggered
   - Waveform is serialized and saved to database cache (`saveWaveform`)

### Current Issues:
- **No user feedback**: Waveforms load silently in background
- **UI available before ready**: Mix can be interacted with before all waveforms loaded
- **No cancellation**: Cannot stop loading if navigating away
- **Performance impact**: Many simultaneous loads can cause lag

## Implementation Steps

### Step 1: Enhance TaskDialog Auto-Close Interface
**Current State**: TaskDialog supports auto-close via `autoCloseOnSuccessDelayMs` parameter. Passing 0 or negative values causes immediate close, positive values add delay, and `std::nullopt` disables auto-close.

**Enhancement Needed**: Create a more expressive API that clearly communicates the auto-close behavior options.

#### 1.1: Add AutoCloseMode enum to TaskDialog.h

```cpp
class TaskDialog : public juce::Component, public juce::Button::Listener, public juce::Timer
{
public:
    enum class AutoCloseMode
    {
        NoAutoClose,      // Dialog stays open, user must click Close
        Immediate,        // Close immediately on success (0ms delay)
        WithDelay         // Close after specified delay
    };
    
    // New constructor with clearer auto-close semantics
    TaskDialog(database::ILongRunningTask* task,
               std::function<void()> onCompletion = nullptr,
               AutoCloseMode closeMode = AutoCloseMode::NoAutoClose,
               int delayMs = 500);  // Default delay when using WithDelay
    
    // Keep old constructor for backward compatibility (deprecated)
    [[deprecated("Use new constructor with AutoCloseMode")]]
    TaskDialog(database::ILongRunningTask* task,
               std::function<void()> onCompletion,
               std::optional<int> autoCloseOnSuccessDelayMs);
    
    // Enhanced static launcher
    static void launch(const juce::String& windowTitle,
                      database::ILongRunningTask* taskToRun,
                      AutoCloseMode closeMode = AutoCloseMode::NoAutoClose,
                      int delayMs = 500,
                      juce::Component* parentToCenterOn = nullptr,
                      std::function<void()> onCompletion = nullptr);
    
    // Keep old launcher for backward compatibility (deprecated)
    [[deprecated("Use new launch() with AutoCloseMode")]]
    static void launch(const juce::String& windowTitle,
                      database::ILongRunningTask* taskToRun,
                      std::optional<int> autoCloseOnSuccessDelayMs,
                      juce::Component* parentToCenterOn = nullptr,
                      std::function<void()> onCompletion = nullptr);
```

#### 1.2: Update Implementation in TaskDialog.cpp

```cpp
// New constructor implementation
TaskDialog::TaskDialog(database::ILongRunningTask* task,
                       std::function<void()> onCompletion,
                       AutoCloseMode closeMode,
                       int delayMs)
    : m_task{task},
      m_onCompletion{std::move(onCompletion)},
      // Convert AutoCloseMode to optional<int> for internal use
      m_autoCloseOnSuccessDelayMs{
          closeMode == AutoCloseMode::NoAutoClose ? std::nullopt :
          closeMode == AutoCloseMode::Immediate ? std::optional<int>(0) :
          std::optional<int>(delayMs)
      },
      // ... rest of initialization
{
    // ... existing constructor body
}

// Backward compatibility constructor
TaskDialog::TaskDialog(database::ILongRunningTask* task,
                       std::function<void()> onCompletion,
                       std::optional<int> autoCloseOnSuccessDelayMs)
    : m_task{task},
      m_onCompletion{std::move(onCompletion)},
      m_autoCloseOnSuccessDelayMs{autoCloseOnSuccessDelayMs},
      // ... rest of initialization
{
    // ... existing constructor body
}

// New static launcher
void TaskDialog::launch(const juce::String& windowTitle,
                       database::ILongRunningTask* taskToRun,
                       AutoCloseMode closeMode,
                       int delayMs,
                       juce::Component* parentToCenterOn,
                       std::function<void()> onCompletion)
{
    if (!taskToRun)
    {
        spdlog::error("TaskDialog::launch: ERROR - taskToRun is nullptr.");
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, 
                                              "Task Error", 
                                              "Cannot start task: no task provided.");
        return;
    }

    auto* dialogComp = new TaskDialog{taskToRun, std::move(onCompletion), closeMode, delayMs};
    
    // ... rest of existing launch implementation
}
```

### Step 2: Create WaveformLoadingTask Class

Create new file: `Database/BackgroundTasks/WaveformLoadingTask.h`

```cpp
#pragma once

#include <Database/Includes/ILongRunningTask.h>
#include <Database/Includes/TrackInfo.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>
#include <memory>

namespace jucyaudio::database::background_tasks {

class WaveformLoadingTask final : public ILongRunningTask
{
public:
    struct WaveformRequest
    {
        TrackId trackId;
        std::filesystem::path filePath;
        bool needsLoading;  // false if already cached
    };

    explicit WaveformLoadingTask(
        std::vector<WaveformRequest> requests,
        juce::AudioFormatManager& formatManager,
        juce::AudioThumbnailCache& thumbnailCache);
    
    ~WaveformLoadingTask() override = default;

    // Returns number of waveforms that were successfully loaded
    int getSuccessCount() const { return m_successCount; }
    
    // Returns tracks that failed to load
    const std::vector<TrackId>& getFailedTracks() const { return m_failedTracks; }

private:
    void run(ProgressCallback progressCb, 
             CompletionCallback completionCb, 
             std::atomic<bool>& shouldCancel) override;

    std::vector<WaveformRequest> m_requests;
    juce::AudioFormatManager& m_formatManager;
    juce::AudioThumbnailCache& m_thumbnailCache;
    
    int m_successCount{0};
    std::vector<TrackId> m_failedTracks;
};

} // namespace
```

### Step 3: Implement WaveformLoadingTask

Create new file: `Database/BackgroundTasks/WaveformLoadingTask.cpp`

Key implementation points:
1. Process waveforms sequentially or in small batches (e.g., 2-3 at a time)
2. Check `shouldCancel` between each waveform
3. Report progress as percentage of total waveforms
4. For each waveform:
   - Skip if already cached (needsLoading = false)
   - Create AudioThumbnail
   - Load from file using FileInputSource
   - Wait for loading to complete (with timeout)
   - Serialize and save to database cache
5. Report completion with summary (e.g., "Loaded 15 of 16 waveforms")

### Step 4: Modify Mix Loading Flow

#### 4.1: Update MixEditorComponent::loadMix()

```cpp
void MixEditorComponent::loadMix(database::MixNode* node)
{
    // ... existing code ...
    
    // After loading mix into playback controller but before populating timeline:
    
    // Collect waveform loading requirements
    auto waveformRequests = collectWaveformRequests(loader);
    
    if (!waveformRequests.empty())
    {
        // Show loading dialog
        auto* task = new WaveformLoadingTask(
            std::move(waveformRequests),
            m_formatManager,
            m_thumbnailCache);
            
        TaskDialog::launch(
            "Loading Waveforms",
            task,
            TaskDialog::AutoCloseMode::Immediate,  // Close immediately on success
            0,  // No delay needed for immediate mode
            this,
            [this, loader]() {
                // After loading completes (or user cancels)
                populateTimeline(loader);
            });
            
        task->release(REFCOUNT_DEBUG_ARGS);
    }
    else
    {
        // All waveforms already cached, populate immediately
        populateTimeline(loader);
    }
}
```

#### 4.2: Add Helper Methods

```cpp
std::vector<WaveformLoadingTask::WaveformRequest> 
MixEditorComponent::collectWaveformRequests(audio::MixProjectLoader* loader)
{
    std::vector<WaveformLoadingTask::WaveformRequest> requests;
    
    for (const auto& mixTrack : loader->getMixTracks())
    {
        if (const auto* trackInfo = loader->getTrackInfoForId(mixTrack.trackId))
        {
            WaveformLoadingTask::WaveformRequest req;
            req.trackId = trackInfo->trackId;
            req.filePath = trackInfo->reconstructFullPath();
            
            // Check if already cached
            std::vector<unsigned char> cachedData;
            req.needsLoading = !theTrackLibrary.loadWaveform(req.trackId, cachedData).isOk() 
                            || cachedData.empty();
            
            requests.push_back(req);
        }
    }
    
    return requests;
}

void MixEditorComponent::populateTimeline(audio::MixProjectLoader* loader)
{
    // Move existing timeline population code here
    if (m_useVirtualTimeline && m_virtualTimeline)
    {
        m_virtualTimeline->loadMixProject(loader);
        // ... rest of virtual timeline setup
    }
    else
    {
        m_timeline.populateFrom(loader);
    }
    
    // ... rest of existing loadMix code
}
```

### Step 5: Optimize Timeline Components

#### 5.1: VirtualTimelineComponent Changes

Modify `loadMixProject()` to accept pre-loaded waveform flag:
- Add parameter `bool waveformsPreloaded = false`
- Skip waveform loading logic if true
- Assume all waveforms are in cache

#### 5.2: TimelineComponent/MixTrackComponent Changes

Similar modifications to skip loading if waveforms are pre-loaded.

### Step 6: Configuration Options

Add to `Settings.h` and config system:
```cpp
struct MixEditingSettings
{
    // ... existing settings ...
    bool preloadWaveformsOnMixOpen{true};  // Show progress dialog
    int waveformLoadingBatchSize{3};       // Process N waveforms at once
    int waveformLoadingTimeoutMs{30000};   // Timeout per waveform
};
```

## Testing Plan

1. **Cache Hit Performance**: Open mix with all waveforms cached - should be instant, no dialog shown
2. **Cache Miss Performance**: Open mix with no cached waveforms - should show progress dialog
3. **Mixed Cache State**: Some cached, some not - progress should reflect only uncached
4. **Cancellation**: Cancel during loading - mix should still open with partial waveforms
5. **Large Mix**: Test with 100+ tracks to verify performance and progress accuracy
6. **Offline Files**: Test with missing audio files - should handle gracefully and report failures
7. **Auto-Close Modes**: 
   - **Immediate**: Dialog closes instantly on success, mix appears ready
   - **WithDelay**: Dialog shows success briefly (test with 1000ms delay)
   - **NoAutoClose**: Dialog remains open until user clicks Close

## Migration Notes

- Existing waveform cache in database remains unchanged
- No database schema changes required
- Backward compatible - can be disabled via settings

## Performance Considerations

1. **Batch Size**: Processing 2-3 waveforms simultaneously balances speed vs system load
2. **Memory**: Each AudioThumbnail uses ~1-2MB RAM during loading
3. **Database I/O**: Cache checks are fast (<1ms per track)
4. **File I/O**: Actual loading is the bottleneck (~100-500ms per file)

## Example Usage Patterns

### Different Auto-Close Behaviors:

```cpp
// 1. Immediate close - for background tasks user doesn't need to see
TaskDialog::launch("Loading Waveforms", 
                  task,
                  TaskDialog::AutoCloseMode::Immediate);

// 2. Brief success message - user sees "Completed!" for 1 second
TaskDialog::launch("Exporting Mix", 
                  task,
                  TaskDialog::AutoCloseMode::WithDelay,
                  1000);  // 1 second delay

// 3. Manual close required - for tasks with important results
TaskDialog::launch("Database Analysis", 
                  task,
                  TaskDialog::AutoCloseMode::NoAutoClose);

// 4. Using old API (deprecated but still works)
TaskDialog::launch("BPM Analysis",
                  task, 
                  500);  // 500ms delay (old style)
```

## Future Enhancements

1. **Intelligent Preloading**: Load visible tracks first, then off-screen tracks
2. **Background Refresh**: Update already-cached waveforms if file modified
3. **Waveform Quality Settings**: Low/Medium/High quality for faster loading
4. **Network Storage**: Special handling for files on network drives
5. **Parallel Loading**: Load multiple waveforms in parallel with thread pool
6. **Progressive Loading**: Show low-res waveforms immediately, refine in background