#include "PerfCounters.h"
#include <spdlog/spdlog.h>

namespace jucyaudio::ui {

//==============================================================================
PerfCounters::PerfCounters()
{
    lastFpsUpdate_ = std::chrono::high_resolution_clock::now();
    lastMetricsUpdate_ = juce::Time::getMillisecondCounter();
}

//==============================================================================
void PerfCounters::beginPaint()
{
#if JUCE_DEBUG
    if (!enabled_)
        return;
    
    paintStartTime_ = std::chrono::high_resolution_clock::now();
#endif
}

void PerfCounters::endPaint()
{
#if JUCE_DEBUG
    if (!enabled_)
        return;
    
    const auto endTime = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - paintStartTime_);
    const double paintTimeMs = duration.count() / 1000.0;
    
    // Update max
    double currentMax = metrics_.maxPaintTimeMs.load();
    while (paintTimeMs > currentMax && 
           !metrics_.maxPaintTimeMs.compare_exchange_weak(currentMax, paintTimeMs))
    {
        // Loop until we successfully update or find a higher value
    }
    
    // Accumulate for average
    paintTimeAccumulator_ += paintTimeMs;
    paintCount_++;
    frameCount_++;
    
    // Update metrics every second
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastMetricsUpdate_ >= metricsUpdateIntervalMs)
    {
        updateAverages();
        updateFps();
        lastMetricsUpdate_ = now;
    }
#endif
}

void PerfCounters::recordCoalescedRepaint()
{
#if JUCE_DEBUG
    if (enabled_)
        metrics_.coalescedRepaints++;
#endif
}

//==============================================================================
void PerfCounters::recordTileHit()
{
#if JUCE_DEBUG
    if (enabled_)
        metrics_.tileCacheHits++;
#endif
}

void PerfCounters::recordTileMiss()
{
#if JUCE_DEBUG
    if (enabled_)
        metrics_.tileCacheMisses++;
#endif
}

void PerfCounters::recordTileEviction()
{
#if JUCE_DEBUG
    if (enabled_)
        metrics_.tileCacheEvictions++;
#endif
}

void PerfCounters::updateTileCacheMemory(size_t bytes)
{
#if JUCE_DEBUG
    if (enabled_)
        metrics_.tileCacheMemoryMB = bytes / (1024 * 1024);
#endif
}

//==============================================================================
void PerfCounters::updateTrackCounts(int visible, int total)
{
#if JUCE_DEBUG
    if (enabled_)
    {
        metrics_.visibleTracks = visible;
        metrics_.totalTracks = total;
    }
#endif
}

//==============================================================================
void PerfCounters::logMetrics() const
{
#if JUCE_DEBUG
    if (!enabled_)
        return;
    
    spdlog::info("=== Performance Metrics ===");
    spdlog::info("Paint: {} paints/sec, {} coalesced, avg {:.2f}ms, max {:.2f}ms",
                metrics_.paintsPerSecond.load(),
                metrics_.coalescedRepaints.load(),
                metrics_.avgPaintTimeMs.load(),
                metrics_.maxPaintTimeMs.load());
    
    const int hits = metrics_.tileCacheHits.load();
    const int misses = metrics_.tileCacheMisses.load();
    const double hitRate = (hits + misses) > 0 ? (100.0 * hits / (hits + misses)) : 0.0;
    
    spdlog::info("Tiles: {:.1f}% hit rate ({} hits, {} misses), {} evictions, {} MB",
                hitRate,
                hits,
                misses,
                metrics_.tileCacheEvictions.load(),
                metrics_.tileCacheMemoryMB.load());
    
    spdlog::info("Tracks: {}/{} visible",
                metrics_.visibleTracks.load(),
                metrics_.totalTracks.load());
    
    spdlog::info("FPS: {:.1f} actual / {:.1f} target",
                metrics_.actualFps.load(),
                metrics_.targetFps.load());
#endif
}

//==============================================================================
void PerfCounters::updateFps()
{
#if JUCE_DEBUG
    const auto now = std::chrono::high_resolution_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFpsUpdate_);
    
    if (elapsed.count() > 0)
    {
        const double fps = (frameCount_ * 1000.0) / elapsed.count();
        metrics_.actualFps = fps;
        metrics_.paintsPerSecond = frameCount_;
    }
    
    frameCount_ = 0;
    lastFpsUpdate_ = now;
#endif
}

void PerfCounters::updateAverages()
{
#if JUCE_DEBUG
    if (paintCount_ > 0)
    {
        metrics_.avgPaintTimeMs = paintTimeAccumulator_ / paintCount_;
        paintTimeAccumulator_ = 0.0;
        paintCount_ = 0;
    }
    
    // Reset coalesced counter
    metrics_.coalescedRepaints = 0;
#endif
}

} // namespace jucyaudio::ui