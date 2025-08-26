#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <chrono>

namespace jucyaudio::ui {

//==============================================================================
/**
    Performance counter utility for tracking UI metrics in debug builds.
    
    All tracking is disabled in release builds for zero overhead.
*/
class PerfCounters
{
public:
    struct Metrics
    {
        // Paint metrics
        std::atomic<int> paintsPerSecond{0};
        std::atomic<int> coalescedRepaints{0};
        std::atomic<double> avgPaintTimeMs{0.0};
        std::atomic<double> maxPaintTimeMs{0.0};
        
        // Tile cache metrics
        std::atomic<int> tileCacheHits{0};
        std::atomic<int> tileCacheMisses{0};
        std::atomic<int> tileCacheEvictions{0};
        std::atomic<size_t> tileCacheMemoryMB{0};
        
        // Track metrics
        std::atomic<int> visibleTracks{0};
        std::atomic<int> totalTracks{0};
        
        // Frame timing
        std::atomic<double> targetFps{60.0};
        std::atomic<double> actualFps{0.0};
        
        void reset()
        {
            paintsPerSecond = 0;
            coalescedRepaints = 0;
            avgPaintTimeMs = 0.0;
            maxPaintTimeMs = 0.0;
            tileCacheHits = 0;
            tileCacheMisses = 0;
            tileCacheEvictions = 0;
            tileCacheMemoryMB = 0;
            visibleTracks = 0;
            totalTracks = 0;
            actualFps = 0.0;
        }
    };
    
    //==============================================================================
    PerfCounters();
    ~PerfCounters() = default;
    
    // Paint tracking
    void beginPaint();
    void endPaint();
    void recordCoalescedRepaint();
    
    // Tile cache tracking
    void recordTileHit();
    void recordTileMiss();
    void recordTileEviction();
    void updateTileCacheMemory(size_t bytes);
    
    // Track tracking
    void updateTrackCounts(int visible, int total);
    
    // Get current metrics
    const Metrics& getMetrics() const { return metrics_; }
    
    // Log current metrics (debug only)
    void logMetrics() const;
    
    // Enable/disable tracking
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
private:
    Metrics metrics_;
    bool enabled_{true};
    
    // Paint timing
    std::chrono::high_resolution_clock::time_point paintStartTime_;
    
    // FPS tracking
    std::chrono::high_resolution_clock::time_point lastFpsUpdate_;
    
    // Update intervals
    static constexpr int metricsUpdateIntervalMs{1000};
    juce::uint32 lastMetricsUpdate_{0};
    
    void updateFps();
    void updateAverages();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PerfCounters)
};

} // namespace jucyaudio::ui