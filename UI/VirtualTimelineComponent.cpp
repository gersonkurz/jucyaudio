#include "VirtualTimelineComponent.h"
#include <Database/Includes/Constants.h>
#include <Database/TrackLibrary.h>
#include <UI/CustomColourIds.h>
#include <UI/Settings.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <algorithm>
#include <limits>

namespace jucyaudio::ui {

//==============================================================================
// Tiling System Classes Implementation
//==============================================================================

class VirtualTimelineComponent::WaveformTileCache
{
public:
    WaveformTileCache(size_t maxMemoryMB)
        : maxMemoryBytes_(maxMemoryMB * 1024 * 1024)
    {
        spdlog::info("[CACHE] WaveformTileCache initialized with {}MB limit", maxMemoryMB);
    }

    std::shared_ptr<WaveformTile> getTile(const WaveformKey& key)
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = cache_.find(key);
        if (it != cache_.end())
        {
            touchTile(key);
            return it->second;
        }
        
        // Debug: log cache miss details
        static int missCount = 0;
        if (++missCount % 100 == 0)
        {
            spdlog::info("[CACHE_MISS] Looking for track {} tile={} stereo={}, cache size={}", 
                        key.trackId, key.tileIndex, key.isStereo, cache_.size());
            
            // Show a few entries from cache for comparison
            int shown = 0;
            for (const auto& [cachedKey, tile] : cache_)
            {
                if (cachedKey.trackId == key.trackId && shown++ < 2)
                {
                    spdlog::info("[CACHE_HAS] Track {} tile={} stereo={}", 
                                cachedKey.trackId, cachedKey.tileIndex, cachedKey.isStereo);
                }
            }
        }
        
        return nullptr;
    }

    void putTile(const WaveformKey& key, juce::Image&& image)
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (cache_.find(key) != cache_.end())
        {
            spdlog::debug("[CACHE] Tile already exists for track {}", key.trackId);
            return; // Already exists
        }

        auto tile = std::make_shared<WaveformTile>();
        tile->image = std::move(image);
        size_t memorySize = static_cast<size_t>(tile->image.getWidth() * tile->image.getHeight() * 4); // RGBA

        while (currentMemoryBytes_ + memorySize > maxMemoryBytes_ && !lruList_.empty())
        {
            evictOldest();
        }

        cache_[key] = tile;
        lruList_.push_front(key);
        lruMap_[key] = lruList_.begin();
        currentMemoryBytes_ += memorySize;
        tile->isReady = true;
        
        // Log cache additions occasionally
        static int addCount = 0;
        if (++addCount % 50 == 0)
        {
            spdlog::info("[CACHE] Added tile for track {}: tile={}, size={}KB (total cache: {} tiles)", 
                         key.trackId, key.tileIndex, memorySize / 1024, cache_.size());
        }
    }

private:
    void touchTile(const WaveformKey& key)
    {
        auto it = lruMap_.find(key);
        if (it != lruMap_.end())
        {
            lruList_.erase(it->second);
            lruList_.push_front(key);
            it->second = lruList_.begin();
        }
    }

    void evictOldest()
    {
        if (lruList_.empty())
            return;

        const auto keyToEvict = lruList_.back();
        lruList_.pop_back();

        auto tileIt = cache_.find(keyToEvict);
        if (tileIt != cache_.end())
        {
            size_t memorySize = static_cast<size_t>(tileIt->second->image.getWidth() * tileIt->second->image.getHeight() * 4);
            currentMemoryBytes_ -= memorySize;
            cache_.erase(tileIt);
        }
        lruMap_.erase(keyToEvict);
    }

    std::unordered_map<WaveformKey, std::shared_ptr<WaveformTile>, WaveformKeyHash> cache_;
    std::list<WaveformKey> lruList_;
    std::unordered_map<WaveformKey, std::list<WaveformKey>::iterator, WaveformKeyHash> lruMap_;
    
    const size_t maxMemoryBytes_;
    std::atomic<size_t> currentMemoryBytes_{0};
    std::mutex cacheMutex_;
};

class VirtualTimelineComponent::TileRenderQueue
{
public:
    struct RenderRequest
    {
        WaveformKey key;
        std::shared_ptr<juce::AudioThumbnail> thumbnail;
        juce::Rectangle<int> tileBoundsInComponent;
        double startTimeSecs;
        double endTimeSecs;
        juce::Colour trackColour;
        double cueStartSecs;
        double trackDurationSecs;
    };

    TileRenderQueue() = default;
    ~TileRenderQueue()
    {
        stop();
    }

    void requestTile(RenderRequest&& request)
    {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            requestQueue_.push(std::move(request));
        }
        queueCondition_.notify_one();
    }

    void start()
    {
        if (renderThread_.joinable())
            return;
        
        shouldStop_ = false;
        renderThread_ = std::thread(&TileRenderQueue::renderThreadProc, this);
    }

    void stop()
    {
        if (!renderThread_.joinable())
            return;

        shouldStop_ = true;
        queueCondition_.notify_all();
        renderThread_.join();
    }

    std::function<void(const WaveformKey&, juce::Image&&)> onTileReady;

private:
    void renderThreadProc()
    {
        while (!shouldStop_)
        {
            RenderRequest request;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                queueCondition_.wait(lock, [this] { return !requestQueue_.empty() || shouldStop_; });

                if (shouldStop_)
                    break;

                request = std::move(requestQueue_.front());
                requestQueue_.pop();
            }

            auto image = renderWaveformTile(request);
            if (onTileReady && !image.isNull())
            {
                onTileReady(request.key, std::move(image));
            }
        }
    }

    juce::Image renderWaveformTile(const RenderRequest& request)
    {
        // Always render tiles at a fixed pixel width for consistency
        // The tile will be stretched when drawn to match the actual zoom level
        constexpr int TILE_RENDER_WIDTH = 512;  // Render at higher res for quality
        const int tileHeight = request.tileBoundsInComponent.getHeight();
        juce::Image tileImage(juce::Image::ARGB, TILE_RENDER_WIDTH, tileHeight, true);
        
        if (!request.thumbnail)
            return tileImage;

        juce::Graphics g(tileImage);
        
        // Use waveform color from theme (matching old implementation)
        const auto& lf = juce::LookAndFeel::getDefaultLookAndFeel();
        g.setColour(lf.findColour(jucyaudio::ui::waveformColourId).withAlpha(0.7f));
        
        // Check if we need stereo rendering
        const bool shouldDrawStereo = request.key.isStereo;
        const int numChannels = request.thumbnail->getNumChannels();
        
        // Use high quality rendering since tiles are now reused across zoom levels
        float verticalZoom = 0.9f;  // Good quality that works well at all zoom levels
        
        // Use the exact time range from the key
        const double startTime = request.key.startTimeSeconds;
        const double endTime = request.key.endTimeSeconds;
        
        // Log only occasionally to reduce overhead
        static int renderCount = 0;
        if (++renderCount % 100 == 0)
        {
            spdlog::info("[TILE_RENDER] Rendering tile for track {}: idx={}, time={:.2f}-{:.2f}, size={}x{}", 
                         request.key.trackId, request.key.tileIndex, startTime, endTime, TILE_RENDER_WIDTH, tileHeight);
        }
        
        if (shouldDrawStereo && numChannels > 1)
        {
            // Split the tile vertically for stereo
            const int halfHeight = tileHeight / 2;
            auto topHalf = juce::Rectangle<int>(0, 0, TILE_RENDER_WIDTH, halfHeight);
            auto bottomHalf = juce::Rectangle<int>(0, halfHeight, TILE_RENDER_WIDTH, tileHeight - halfHeight);
            
            request.thumbnail->drawChannel(g, topHalf, startTime, endTime, 0, verticalZoom);
            request.thumbnail->drawChannel(g, bottomHalf, startTime, endTime, 1, verticalZoom);
        }
        else
        {
            // Single waveform view - draw channel 0 only
            request.thumbnail->drawChannel(g, tileImage.getBounds(), startTime, endTime, 0, verticalZoom);
        }
        
        return tileImage;
    }

    std::queue<RenderRequest> requestQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    std::thread renderThread_;
    std::atomic<bool> shouldStop_{false};
};


//==============================================================================
VirtualTimelineComponent::VirtualTimelineComponent(juce::AudioFormatManager& formatManager,
                                                   juce::AudioThumbnailCache& thumbnailCache)
    : formatManager_(formatManager)
    , thumbnailCache_(thumbnailCache)
{
    setOpaque(true);  // Critical for performance - we paint the entire background
    setWantsKeyboardFocus(true);  // Enable keyboard shortcuts
    metricsResetTime_ = juce::Time::getMillisecondCounter();

    tileCache_ = std::make_unique<WaveformTileCache>(512); // 512MB cache for more tiles
    tileRenderer_ = std::make_unique<TileRenderQueue>();
    tileRenderer_->onTileReady = [this](const WaveformKey& key, juce::Image&& image) {
        onTileRendered(key, std::move(image));
    };
    tileRenderer_->start();
}

VirtualTimelineComponent::~VirtualTimelineComponent()
{
    // Remove change listeners from all thumbnails
    for (auto& track : tracks_)
    {
        if (track.thumbnail)
        {
            track.thumbnail->removeChangeListener(this);
        }
    }
    
    tileRenderer_->stop();
    cancelPendingUpdate();
}

//==============================================================================
void VirtualTimelineComponent::paint(juce::Graphics& g)
{
    const auto startTime = std::chrono::high_resolution_clock::now();
    
    // Early exit if nothing to paint
    const auto clipBounds = g.getClipBounds();
    if (clipBounds.isEmpty())
        return;
    
    // Background
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    
    // Paint grid lines
    paintGrid(g);
    
    // Paint visible tracks only
    paintTracks(g);
    
    // Paint playhead last (on top)
    paintPlayhead(g);
    
    // Update performance metrics
    const auto endTime = std::chrono::high_resolution_clock::now();
    const auto paintTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
    
    // Update rolling average
    const double newAvg = (metrics_.avgPaintTimeMs * paintCount_ + paintTime / 1000.0) / (paintCount_ + 1);
    metrics_.avgPaintTimeMs = newAvg;
    paintCount_++;
    
    // Update paints per second every second
    const auto now = juce::Time::getMillisecondCounter();
    if (now - metricsResetTime_ >= 1000)
    {
        metrics_.paintsPerSecond = paintCount_;
        paintCount_ = 0;
        metricsResetTime_ = now;
        
#if JUCE_DEBUG
        spdlog::info("VirtualTimeline: {} paints/sec, avg {:.2f}ms, {} visible tracks",
                    metrics_.paintsPerSecond.load(),
                    metrics_.avgPaintTimeMs.load(),
                    metrics_.visibleTracks.load());
#endif
    }
}

void VirtualTimelineComponent::resized()
{
    spdlog::info("VirtualTimelineComponent::resized() called - size: {}x{}", getWidth(), getHeight());
    
    // Recalculate track positions when window is resized
    // This will re-layout tracks to use new available lanes
    calculateTrackPositions();
    refreshLayout();
    queueVisibleTiles();
    scheduleFrame();
}

//==============================================================================
void VirtualTimelineComponent::mouseDown(const juce::MouseEvent& event)
{
    // Always grab keyboard focus when clicked
    grabKeyboardFocus();
    
    // Handle right-click for context menu
    if (event.mods.isRightButtonDown())
    {
        const auto hitInfo = hitTest(event.position.toInt());
        
        // Select the track if we right-clicked on one
        if (hitInfo.track)
        {
            const bool addToSelection = event.mods.isShiftDown() || event.mods.isCommandDown();
            selectTrack(hitInfo.track->id, addToSelection);
            scheduleFrame();
        }
        
        // Show context menu
        showContextMenu(event.position.toInt());
        return;
    }
    
    // Handle double-click for playback positioning
    if (event.mods.isLeftButtonDown())
    {
        // Convert pixel position to time
        const double clickTime = event.position.x / pixelsPerSecond_;
        
        spdlog::info("[VirtualTimeline] Click at time: {:.2f}s (x={}, pixelsPerSecond={}, clicks={})", 
                    clickTime, event.position.x, pixelsPerSecond_, event.getNumberOfClicks());
        
        // Store the clicked position (stays visible until next click)
        clickedTimePosition_ = clickTime;
        
        // Handle double-click to start playback (always plays)
        if (event.getNumberOfClicks() == 2)
        {
            spdlog::info("[VirtualTimeline] Double-click detected - starting playback at {:.2f}s", clickTime);
            if (onMixPlaybackAlwaysRequested)
            {
                onMixPlaybackAlwaysRequested(clickTime);
            }
        }
        else if (event.getNumberOfClicks() == 1)
        {
            // Single click - just seek (but keep position visible)
            if (onSeekRequested)
            {
                onSeekRequested(clickTime);
            }
        }
        
        // Repaint to show the clicked position
        repaint();
    }
    
    const auto hitInfo = hitTest(event.position.toInt());
    
    if (hitInfo.track)
    {
        const bool addToSelection = event.mods.isShiftDown() || event.mods.isCommandDown();
        
        // Check what we're clicking on
        if (hitInfo.type == HitTestResult::AttachFrom || 
            hitInfo.type == HitTestResult::AttachTo ||
            hitInfo.type == HitTestResult::EnvelopePoint ||
            hitInfo.type == HitTestResult::CueStart ||
            hitInfo.type == HitTestResult::CueEnd)
        {
            // Start dragging an attach point, envelope point, or cue point
            dragState_.isDragging = false;  // Will be set true in mouseDrag
            dragState_.trackId = hitInfo.track->id;
            dragState_.dragStartPoint = event.position.toInt();
            
            if (hitInfo.type == HitTestResult::CueStart)
            {
                dragState_.dragType = DragState::DragType::CueStart;
                if (hitInfo.track->mixTrack)
                {
                    dragState_.originalTime = std::chrono::duration<double>(
                        hitInfo.track->mixTrack->cueStart).count();
                }
            }
            else if (hitInfo.type == HitTestResult::CueEnd)
            {
                dragState_.dragType = DragState::DragType::CueEnd;
                if (hitInfo.track->mixTrack && hitInfo.track->trackInfo)
                {
                    dragState_.originalTime = std::chrono::duration<double>(
                        hitInfo.track->mixTrack->getCueEndActual(hitInfo.track->trackInfo->duration)).count();
                }
            }
            else if (hitInfo.type == HitTestResult::AttachFrom)
            {
                dragState_.dragType = DragState::DragType::AttachFrom;
                if (hitInfo.track->mixTrack)
                {
                    dragState_.originalTime = std::chrono::duration<double>(
                        hitInfo.track->mixTrack->attachFrom).count();
                }
            }
            else if (hitInfo.type == HitTestResult::AttachTo)
            {
                dragState_.dragType = DragState::DragType::AttachTo;
                if (hitInfo.track->mixTrack)
                {
                    dragState_.originalTime = std::chrono::duration<double>(
                        hitInfo.track->mixTrack->attachTo).count();
                }
            }
            else if (hitInfo.type == HitTestResult::EnvelopePoint)
            {
                dragState_.dragType = DragState::DragType::EnvelopePoint;
                dragState_.draggedPointIndex = hitInfo.pointIndex;
                if (hitInfo.track->mixTrack && hitInfo.pointIndex >= 0 && 
                    hitInfo.pointIndex < static_cast<int>(hitInfo.track->mixTrack->envelopePoints.size()))
                {
                    dragState_.originalTime = std::chrono::duration<double>(
                        hitInfo.track->mixTrack->envelopePoints[hitInfo.pointIndex].time).count();
                }
            }
        }
        
        selectTrack(hitInfo.track->id, addToSelection);
        scheduleFrame();
    }
    else if (!event.mods.isShiftDown())
    {
        clearSelection();
        dragState_ = {};  // Reset drag state
        scheduleFrame();
    }
}

void VirtualTimelineComponent::mouseDrag(const juce::MouseEvent& event)
{
    // Check if we should start dragging
    if (!dragState_.isDragging && dragState_.dragType != DragState::DragType::None)
    {
        // Start drag after moving a minimum distance (8 pixels to avoid accidental drags)
        const auto distance = event.position.toInt().getDistanceFrom(dragState_.dragStartPoint);
        if (distance > 8.0f)
        {
            dragState_.isDragging = true;
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            
            spdlog::info("[DRAG_START] Starting drag: type={}, trackId={}, startPoint=({},{}), currentPoint=({},{})",
                        static_cast<int>(dragState_.dragType),
                        dragState_.trackId,
                        dragState_.dragStartPoint.x, dragState_.dragStartPoint.y,
                        event.position.toInt().x, event.position.toInt().y);
        }
    }
    
    if (dragState_.isDragging)
    {
        // Calculate new time position based on horizontal mouse movement
        const int deltaX = event.position.toInt().x - dragState_.dragStartPoint.x;
        const double deltaTime = deltaX / pixelsPerSecond_;
        dragState_.currentTime = dragState_.originalTime + deltaTime;
        
        // Find the track being edited
        TrackRenderData* track = nullptr;
        for (auto& t : tracks_)
        {
            if (t.id == dragState_.trackId)
            {
                track = &t;
                break;
            }
        }
        
        if (track && track->mixTrack)
        {
            // Clamp the time to valid range based on what we're dragging
            if (dragState_.dragType == DragState::DragType::CueStart)
            {
                // For cue start, we're dragging the left edge of the track
                // Calculate what the new cue start should be based on mouse position
                const double mouseTime = pixelsToSeconds(event.position.toInt().x);
                const double audioStartTime = std::chrono::duration<double>(track->audioStartTime).count();
                
                // The new cue start is the difference between where the mouse is and where the audio starts
                const double newCueStart = mouseTime - audioStartTime;
                
                // Constrain cue start
                const double minCueStart = -600.0; // Allow up to 10 minutes of silence before (was 60 seconds)
                const double maxCueStart = std::chrono::duration<double>(track->trackInfo->duration).count() - 1.0; // Leave at least 1 second
                const double clampedCueStart = juce::jlimit(minCueStart, maxCueStart, newCueStart);
                
                // Update the cue start
                track->mixTrack->cueStart = jucyaudio::Duration_t(
                    static_cast<int64_t>(clampedCueStart * 1000));
                
                // Update component start time and effective duration
                track->componentStartTime = track->audioStartTime + track->mixTrack->cueStart;
                track->effectiveDuration = std::chrono::duration<double>(
                    track->mixTrack->getEffectiveDuration(track->trackInfo->duration)).count();
                    
                // Recalculate track bounds
                const double startTime = std::chrono::duration<double>(track->componentStartTime).count();
                const int startX = static_cast<int>(startTime * pixelsPerSecond_);
                const int width = static_cast<int>(track->effectiveDuration * pixelsPerSecond_);
                track->bounds = juce::Rectangle<int>(startX, track->bounds.getY(), width, track->bounds.getHeight());
                track->waveformBounds = track->bounds.reduced(waveformInset);
                
                // Schedule repaint
                scheduleFrame();
            }
            else if (dragState_.dragType == DragState::DragType::CueEnd)
            {
                // For cue end, we're dragging the right edge of the track
                // Calculate what the new effective duration should be based on mouse position
                const double mouseTime = pixelsToSeconds(event.position.toInt().x);
                const double componentStart = std::chrono::duration<double>(track->componentStartTime).count();
                const double newEffectiveDuration = mouseTime - componentStart;
                
                // Calculate what cueEnd should be to achieve this effective duration
                // effectiveDuration = (cueEnd - cueStart) where cueEnd is the actual end position
                // So: actualCueEnd = effectiveDuration + cueStart
                // And: cueEnd (offset from track end) = actualCueEnd - trackDuration
                const double actualCueEnd = newEffectiveDuration + std::chrono::duration<double>(track->mixTrack->cueStart).count();
                const double newCueEnd = actualCueEnd - std::chrono::duration<double>(track->trackInfo->duration).count();
                
                // Constrain cue end
                const double minCueEnd = -std::chrono::duration<double>(track->trackInfo->duration).count() + 1.0; // At least 1 second
                const double maxCueEnd = 600.0; // Allow up to 10 minutes of silence after (was 60 seconds)
                const double clampedCueEnd = juce::jlimit(minCueEnd, maxCueEnd, newCueEnd);
                
                // Update the cue end
                track->mixTrack->cueEnd = jucyaudio::Duration_t(
                    static_cast<int64_t>(clampedCueEnd * 1000));
                
                // Update effective duration
                track->effectiveDuration = std::chrono::duration<double>(
                    track->mixTrack->getEffectiveDuration(track->trackInfo->duration)).count();
                    
                // Recalculate track bounds (only width changes for cue end)
                const int width = static_cast<int>(track->effectiveDuration * pixelsPerSecond_);
                track->bounds = juce::Rectangle<int>(track->bounds.getX(), track->bounds.getY(), width, track->bounds.getHeight());
                track->waveformBounds = track->bounds.reduced(waveformInset);
                
                // Schedule repaint
                scheduleFrame();
            }
            else if (dragState_.dragType == DragState::DragType::AttachFrom)
            {
                // AttachFrom must be within track bounds
                const double minTime = std::chrono::duration<double>(track->mixTrack->cueStart).count();
                const double maxTime = std::chrono::duration<double>(
                    track->mixTrack->getCueEndActual(track->trackInfo->duration)).count();
                dragState_.currentTime = juce::jlimit(minTime, maxTime, dragState_.currentTime);
                
                // Update the attach point (temporarily for preview)
                // Convert seconds to milliseconds (Duration_t is std::chrono::milliseconds)
                track->mixTrack->attachFrom = jucyaudio::Duration_t(
                    static_cast<int64_t>(dragState_.currentTime * 1000));
            }
            else if (dragState_.dragType == DragState::DragType::AttachTo)
            {
                // AttachTo must be within track bounds
                const double minTime = std::chrono::duration<double>(track->mixTrack->cueStart).count();
                const double maxTime = std::chrono::duration<double>(
                    track->mixTrack->getCueEndActual(track->trackInfo->duration)).count();
                dragState_.currentTime = juce::jlimit(minTime, maxTime, dragState_.currentTime);
                
                // Update the attach point (temporarily for preview)
                // Convert seconds to milliseconds (Duration_t is std::chrono::milliseconds)
                track->mixTrack->attachTo = jucyaudio::Duration_t(
                    static_cast<int64_t>(dragState_.currentTime * 1000));
            }
            else if (dragState_.dragType == DragState::DragType::EnvelopePoint)
            {
                // Update envelope point time and volume
                if (dragState_.draggedPointIndex >= 0 && 
                    dragState_.draggedPointIndex < static_cast<int>(track->mixTrack->envelopePoints.size()))
                {
                    // Get the current envelope point's original values
                    const double originalPointTime = std::chrono::duration<double>(
                        track->mixTrack->envelopePoints[dragState_.draggedPointIndex].time).count();
                    const float originalVolume = static_cast<float>(
                        track->mixTrack->envelopePoints[dragState_.draggedPointIndex].volume) / 1000.0f;
                    
                    // HORIZONTAL: Update time based on X position
                    // Convert mouse X position to timeline time
                    const double timelineTime = event.position.x / pixelsPerSecond_;
                    
                    // Convert timeline time to track-local time (relative to audio start)
                    const double trackLocalTime = timelineTime - std::chrono::duration<double>(track->audioStartTime).count();
                    
                    // Envelope points are relative to the track's audio, and must be within cue bounds
                    const double minTime = std::chrono::duration<double>(track->mixTrack->cueStart).count();
                    const double maxTime = std::chrono::duration<double>(
                        track->mixTrack->getCueEndActual(track->trackInfo->duration)).count();
                    const double clampedTime = juce::jlimit(minTime, maxTime, trackLocalTime);
                    
                    // VERTICAL: Update volume based on Y position
                    // Calculate normalized volume from mouse Y position (1.0 = top, 0.0 = bottom)
                    const float relativeY = event.position.y - track->waveformBounds.getY();
                    const float normalizedY = relativeY / static_cast<float>(track->waveformBounds.getHeight());
                    const float newVolume = juce::jlimit(0.0f, 1.0f, 1.0f - normalizedY);
                    
                    // Log detailed info for debugging
                    spdlog::info("[ENVELOPE_DRAG] Point {}: mousePos=({:.1f},{:.1f}), time: {:.3f}s -> {:.3f}s, volume: {:.3f} -> {:.3f}",
                                dragState_.draggedPointIndex,
                                event.position.x, event.position.y,
                                originalPointTime, clampedTime,
                                originalVolume, newVolume);
                    
                    // Store the clamped time for the log message in mouseUp
                    dragState_.currentTime = clampedTime;
                    
                    // Update the envelope point
                    // Convert seconds to milliseconds (Duration_t is std::chrono::milliseconds)
                    track->mixTrack->envelopePoints[dragState_.draggedPointIndex].time = 
                        jucyaudio::Duration_t(static_cast<int64_t>(clampedTime * 1000));
                    
                    // Convert normalized volume to Volume_t (integer where 1000 = 1.0)
                    track->mixTrack->envelopePoints[dragState_.draggedPointIndex].volume = 
                        static_cast<jucyaudio::Volume_t>(newVolume * 1000);
                }
            }
        }
        
        repaint();
    }
}

void VirtualTimelineComponent::mouseUp(const juce::MouseEvent& event)
{
    if (dragState_.isDragging)
    {
        // Find the track that was modified
        TrackRenderData* modifiedTrack = nullptr;
        for (auto& track : tracks_)
        {
            if (track.id == dragState_.trackId)
            {
                modifiedTrack = &track;
                break;
            }
        }
        
        if (modifiedTrack && modifiedTrack->mixTrack)
        {
            // Notify about changes through callbacks
            if (dragState_.dragType == DragState::DragType::CueStart || 
                dragState_.dragType == DragState::DragType::CueEnd)
            {
                spdlog::info("Cue point ({}) moved to {:.3f}s for track {}", 
                            dragState_.dragType == DragState::DragType::CueStart ? "start" : "end",
                            dragState_.currentTime, dragState_.trackId);
                
                // Recalculate track positions after cue change
                calculateTrackPositions();
                
                // Notify about the cue point change
                if (onCuePointsChanged)
                {
                    onCuePointsChanged(modifiedTrack->mixTrack->orderInMix,
                                     modifiedTrack->mixTrack->cueStart,
                                     modifiedTrack->mixTrack->cueEnd);
                }
            }
            else if (dragState_.dragType == DragState::DragType::AttachFrom || 
                     dragState_.dragType == DragState::DragType::AttachTo)
            {
                spdlog::info("Attach point moved to {:.3f}s for track {}", 
                            dragState_.currentTime, dragState_.trackId);
                
                // Recalculate track positions first
                calculateTrackPositions();
                
                // Notify about the change
                if (onCueAttachChanged)
                {
                    onCueAttachChanged(modifiedTrack->mixTrack->orderInMix, *modifiedTrack->mixTrack);
                }
            }
            else if (dragState_.dragType == DragState::DragType::EnvelopePoint)
            {
                spdlog::info("Envelope point {} moved to {:.3f}s for track {}", 
                            dragState_.draggedPointIndex, dragState_.currentTime, dragState_.trackId);
                
                // Notify about envelope change
                if (onEnvelopeChanged)
                {
                    onEnvelopeChanged(modifiedTrack->mixTrack->orderInMix, modifiedTrack->mixTrack->envelopePoints);
                }
            }
        }
    }
    
    // Reset drag state
    dragState_ = {};
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void VirtualTimelineComponent::mouseMove(const juce::MouseEvent& event)
{
    // Update cursor based on what we're hovering over
    const auto hitInfo = hitTest(event.position.toInt());
    
    if (hitInfo.type == HitTestResult::CueStart || 
        hitInfo.type == HitTestResult::CueEnd)
    {
        // Use resize cursor for cue point dragging (track edge dragging)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
    else if (hitInfo.type == HitTestResult::AttachFrom || 
             hitInfo.type == HitTestResult::AttachTo ||
             hitInfo.type == HitTestResult::EnvelopePoint)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    else
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void VirtualTimelineComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    // Check if Ctrl/Cmd is held for zooming
    const bool isZooming = event.mods.isCommandDown() || event.mods.isCtrlDown();
    
    if (isZooming)
    {
        // Zoom behavior with Ctrl/Cmd held
        constexpr double ZOOM_FACTOR = 1.1;
        constexpr double MIN_ZOOM = 1.0;    // 1 pixel per second (zoomed out)
        constexpr double MAX_ZOOM = 2000.0; // 2000 pixels per second (zoomed in)
        
        // Calculate new zoom level
        const double zoomDelta = wheel.deltaY > 0 ? ZOOM_FACTOR : (1.0 / ZOOM_FACTOR);
        const double newZoom = juce::jlimit(MIN_ZOOM, MAX_ZOOM, pixelsPerSecond_ * zoomDelta);
        
        if (newZoom != pixelsPerSecond_)
        {
            if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
            {
                // Always use the center of the viewport as the zoom anchor point
                const int viewportCenterX = viewport->getViewPositionX() + viewport->getWidth() / 2;
                const double centerTime = pixelsToSeconds(viewportCenterX);
                
                // Update zoom level
                pixelsPerSecond_ = newZoom;
                calculateTrackPositions();
                
                // Calculate where the center time is now in pixels after zoom
                const int newCenterPixel = secondsToPixels(centerTime);
                
                // Adjust viewport to keep the center time at the center of the viewport
                const int newViewportX = newCenterPixel - viewport->getWidth() / 2;
                
                // Clamp to valid range
                const int maxScrollX = juce::jmax(0, getWidth() - viewport->getWidth());
                viewport->setViewPosition(juce::jlimit(0, maxScrollX, newViewportX), viewport->getViewPositionY());
            }
            else
            {
                // Fallback if no viewport
                pixelsPerSecond_ = newZoom;
                calculateTrackPositions();
            }
            
            queueVisibleTiles();
            scheduleFrame();
        }
    }
    else
    {
        // Horizontal scrolling behavior (default)
        if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
        {
            // Scroll horizontally with the mouse wheel
            // Use deltaY for horizontal scrolling (most common for horizontal timeline scrolling)
            const int scrollAmount = static_cast<int>(wheel.deltaY * -50); // Negative to match natural scrolling
            const int currentX = viewport->getViewPositionX();
            const int newX = juce::jmax(0, currentX + scrollAmount);
            
            viewport->setViewPosition(newX, viewport->getViewPositionY());
        }
    }
}

//==============================================================================
void VirtualTimelineComponent::handleAsyncUpdate()
{
    // Frame scheduler - coalesces multiple repaint requests
    repaint();
}

void VirtualTimelineComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    // Called when a thumbnail finishes loading
    // Check if it's one of our thumbnails
    for (const auto& track : tracks_)
    {
        if (track.thumbnail.get() == source)
        {
            auto* thumbnail = static_cast<juce::AudioThumbnail*>(source);
            if (thumbnail->isFullyLoaded())
            {
                spdlog::info("Waveform loaded for track {}, queuing tiles for generation", track.id);
                
                // Queue tiles for this track now that the waveform is loaded
                queueVisibleTiles();
                
                // Trigger a repaint to show the newly loaded waveform
                scheduleFrame();
                
                // Save the waveform to cache for next time
                if (track.trackInfo)
                {
                    juce::MemoryOutputStream stream;
                    thumbnail->saveTo(stream);
                    
                    if (stream.getDataSize() > 0)
                    {
                        const auto* data = static_cast<const unsigned char*>(stream.getData());
                        std::vector<unsigned char> waveformData(data, data + stream.getDataSize());
                        database::theTrackLibrary.saveWaveform(track.trackInfo->trackId, waveformData);
                        spdlog::info("Saved waveform to cache for track {}", track.trackInfo->trackId);
                    }
                }
            }
            break;
        }
    }
}

void VirtualTimelineComponent::scheduleFrame()
{
    // Coalesce repaint requests via AsyncUpdater
    triggerAsyncUpdate();
}

bool VirtualTimelineComponent::keyPressed(const juce::KeyPress& key)
{
    spdlog::info("[KEYBOARD] VirtualTimelineComponent::keyPressed - key code: {}, text: '{}', modifiers: cmd={} shift={} alt={} ctrl={}", 
                 key.getKeyCode(), 
                 key.getTextDescription().toStdString(),
                 key.getModifiers().isCommandDown(),
                 key.getModifiers().isShiftDown(),
                 key.getModifiers().isAltDown(),
                 key.getModifiers().isCtrlDown());
    
    if (key == juce::KeyPress::spaceKey)
    {
        spdlog::info("Space key pressed - toggling playback");
        // Play/pause from clicked position (or current playhead, or 0 as fallback)
        if (onMixPlaybackRequested)
        {
            double playPosition = 0.0;
            if (clickedTimePosition_ >= 0.0)
            {
                playPosition = clickedTimePosition_;  // Use last clicked position
            }
            else if (playheadSeconds_ >= 0.0)
            {
                playPosition = playheadSeconds_;  // Or current playhead position
            }
            spdlog::info("Starting playback from position: {:.2f}s", playPosition);
            onMixPlaybackRequested(playPosition);
        }
        return true;
    }
    else if (key == juce::KeyPress::escapeKey)
    {
        spdlog::info("Escape key pressed - stopping playback");
        // Stop playback
        if (onMixPlaybackRequested)
        {
            onMixPlaybackRequested(-1.0); // Special value to indicate stop
        }
        return true;
    }
    else if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        spdlog::info("Delete/Backspace key pressed");
        if (!selectedTracks_.empty())
        {
            spdlog::info("Requesting deletion of {} selected track(s)", selectedTracks_.size());
            if (onDeleteTracksRequested)
            {
                onDeleteTracksRequested();
            }
        }
        return true;
    }
    else if (key == juce::KeyPress('x', juce::ModifierKeys::commandModifier, 0))
    {
        spdlog::info("Cmd+X (cut) pressed");
        if (!selectedTracks_.empty())
        {
            copySelectedTracks();
            deleteSelectedTracks();
        }
        return true;
    }
    else if (key == juce::KeyPress('c', juce::ModifierKeys::commandModifier, 0))
    {
        spdlog::info("[KEYBOARD] Cmd+C (copy) detected - selected tracks: {}", selectedTracks_.size());
        if (!selectedTracks_.empty())
        {
            spdlog::info("[KEYBOARD] Calling copySelectedTracks()");
            copySelectedTracks();
        }
        else
        {
            spdlog::warn("[KEYBOARD] No tracks selected for copy");
        }
        return true;
    }
    else if (key == juce::KeyPress('x', juce::ModifierKeys::commandModifier, 0))
    {
        spdlog::info("[KEYBOARD] Cmd+X (cut) detected - selected tracks: {}", selectedTracks_.size());
        if (!selectedTracks_.empty())
        {
            spdlog::info("[KEYBOARD] Calling copySelectedTracks() then deleteSelectedTracks()");
            copySelectedTracks();
            deleteSelectedTracks();
        }
        else
        {
            spdlog::warn("[KEYBOARD] No tracks selected for cut");
        }
        return true;
    }
    else if (key == juce::KeyPress('v', juce::ModifierKeys::commandModifier, 0))
    {
        spdlog::info("[KEYBOARD] Cmd+V (paste) detected - clipboard has {} tracks, {} tracks selected", 
                    clipboardTracks_.size(), selectedTracks_.size());
        if (!clipboardTracks_.empty() && !selectedTracks_.empty())
        {
            spdlog::info("[KEYBOARD] Calling pasteTracksAfterSelection()");
            pasteTracksAfterSelection();
        }
        else
        {
            spdlog::warn("[KEYBOARD] Cannot paste - clipboard empty: {}, no selection: {}",
                        clipboardTracks_.empty(), selectedTracks_.empty());
        }
        return true;
    }
    
    return false; // Let parent handle other keys
}

//==============================================================================
void VirtualTimelineComponent::loadMixProject(audio::MixProjectLoader* loader)
{
    // Remove change listeners from existing thumbnails before clearing
    for (auto& track : tracks_)
    {
        if (track.thumbnail)
        {
            track.thumbnail->removeChangeListener(this);
        }
    }
    
    mixProjectLoader_ = loader;
    tracks_.clear();
    selectedTracks_.clear();
    
    if (!loader)
    {
        metrics_.totalTracks = 0;
        scheduleFrame();
        return;
    }
    
    // Convert MixTracks to TrackRenderData using the Mix Flow algorithm
    auto& mixTracks = loader->getMixTracks();
    spdlog::info("[VIRTUAL_TIMELINE] Loading {} mix tracks into virtual timeline", mixTracks.size());
    tracks_.reserve(mixTracks.size());
    
    // Calculate global offset from first track's cueStart (matching original timeline)
    jucyaudio::Duration_t globalOffset{0};
    if (!mixTracks.empty() && mixTracks[0].cueStart < jucyaudio::Duration_t{0})
    {
        globalOffset = -mixTracks[0].cueStart;
    }
    
    jucyaudio::Duration_t previousAudioStartTime{0};
    
    for (size_t i = 0; i < mixTracks.size(); ++i)
    {
        const auto& mixTrack = mixTracks[i];
        TrackRenderData data;
        // Use the index in the tracks_ vector as the unique ID. This provides a stable
        // identifier that doesn't change when tracks are reordered in the database.
        // The index corresponds to the visual order in the timeline.
        data.id = static_cast<int>(i);
        data.mixTrack = std::make_shared<database::MixTrack>(mixTrack);
        
        // Get track info
        data.trackInfo = loader->getTrackInfoForId(mixTrack.trackId);
        if (data.trackInfo)
        {
            data.name = juce::String(data.trackInfo->title);
            data.effectiveDuration = std::chrono::duration<double>(
                mixTrack.getEffectiveDuration(data.trackInfo->duration)).count();
        }
        else
        {
            data.name = juce::String("Track ") + juce::String(static_cast<int>(i) + 1);
            data.effectiveDuration = 180.0; // Default 3 minutes
        }
        
        // Calculate time positions using Mix Flow algorithm (matching original timeline)
        if (i == 0)
        {
            data.audioStartTime = globalOffset;
        }
        else
        {
            const auto& prevTrack = mixTracks[i - 1];
            data.audioStartTime = previousAudioStartTime + prevTrack.attachTo - mixTrack.attachFrom;
        }
        
        data.componentStartTime = data.audioStartTime + mixTrack.cueStart;
        previousAudioStartTime = data.audioStartTime;
        
        // Track color will be determined by theme during painting
        data.colour = juce::Colour();  // Not used - we'll use theme colors
        
        // --- PHASE 3, STEP 1: Create and load AudioThumbnail ---
        if (data.trackInfo)
        {
            data.thumbnail = std::make_shared<juce::AudioThumbnail>(512, formatManager_, thumbnailCache_);
            
            std::vector<unsigned char> cachedWaveformVec;
            auto& db = database::theTrackLibrary;
            if (db.loadWaveform(data.trackInfo->trackId, cachedWaveformVec).isOk() && !cachedWaveformVec.empty())
            {
                juce::MemoryBlock mb{cachedWaveformVec.data(), cachedWaveformVec.size()};
                juce::MemoryInputStream stream{mb, false};
                if (data.thumbnail->loadFrom(stream))
                {
                    spdlog::info("[PHASE 3] Loaded waveform for track {} from DB cache.", data.trackInfo->trackId);
                }
                else
                {
                    spdlog::warn("[PHASE 3] FAILED to load cached waveform for track {}. Will try loading from file.", data.trackInfo->trackId);
                    juce::File audioFile(data.trackInfo->reconstructFullPath().string());
                    if (audioFile.existsAsFile())
                    {
                        data.thumbnail->setSource(new juce::FileInputSource(audioFile));
                        data.thumbnail->addChangeListener(this);  // Get notified when loading completes
                        spdlog::info("[PHASE 3] Loading waveform from file for track {} (async)", data.trackInfo->trackId);
                    }
                }
            }
            else
            {
                spdlog::warn("[PHASE 3] No waveform in DB cache for track {}. Loading from file.", data.trackInfo->trackId);
                juce::File audioFile(data.trackInfo->reconstructFullPath().string());
                if (audioFile.existsAsFile())
                {
                    data.thumbnail->setSource(new juce::FileInputSource(audioFile));
                    data.thumbnail->addChangeListener(this);  // Get notified when loading completes
                    spdlog::info("[PHASE 3] Loading waveform from file for track {} (async)", data.trackInfo->trackId);
                }
            }
        }
        
        tracks_.push_back(std::move(data));
    }
    
    spdlog::info("[VIRTUAL_TIMELINE] Created {} TrackRenderData entries", tracks_.size());
    for (size_t i = 0; i < std::min(size_t(5), tracks_.size()); ++i)
    {
        spdlog::info("[VIRTUAL_TIMELINE]   Track[{}]: id={}, trackId={}, orderInMix={}", 
                    i, tracks_[i].id, 
                    tracks_[i].mixTrack ? tracks_[i].mixTrack->trackId : -1,
                    tracks_[i].mixTrack ? tracks_[i].mixTrack->orderInMix : -1);
    }
    if (tracks_.size() > 5)
    {
        spdlog::info("[VIRTUAL_TIMELINE]   ... and {} more tracks", tracks_.size() - 5);
    }
    
    metrics_.totalTracks = static_cast<int>(tracks_.size());
    calculateTrackPositions();
    updateVisibleTracks();
    queueVisibleTiles();  // Start generating tiles immediately after loading
    scheduleFrame();
    
    // Force immediate repaint after loading new tracks
    // This ensures the timeline updates visually after paste operations
    repaint();
}

void VirtualTimelineComponent::setZoomLevel(double secondsPerPixel)
{
    if (secondsPerPixel > 0.0)
    {
        const double newPixelsPerSecond = 1.0 / secondsPerPixel;
        setPixelsPerSecond(newPixelsPerSecond);
    }
}

void VirtualTimelineComponent::setPixelsPerSecond(double pixelsPerSecond)
{
    if (pixelsPerSecond != pixelsPerSecond_ && pixelsPerSecond > 0.0)
    {
        pixelsPerSecond_ = pixelsPerSecond;
        calculateTrackPositions();
        scheduleFrame();
    }
}

void VirtualTimelineComponent::setViewportBounds(const juce::Rectangle<int>& bounds)
{
    if (bounds != viewportBounds_)
    {
        const bool heightChanged = bounds.getHeight() != viewportBounds_.getHeight();
        spdlog::info("VirtualTimelineComponent::setViewportBounds() - old: {}x{}, new: {}x{}, heightChanged: {}",
                     viewportBounds_.getWidth(), viewportBounds_.getHeight(),
                     bounds.getWidth(), bounds.getHeight(), heightChanged);
        
        viewportBounds_ = bounds;
        
        // If height changed, recalculate track positions to adjust lanes
        if (heightChanged && !tracks_.empty())
        {
            spdlog::info("  Recalculating track positions due to height change");
            calculateTrackPositions();
        }
        
        updateVisibleTracks();
        queueVisibleTiles();
        scheduleFrame();
    }
}

void VirtualTimelineComponent::setPlayheadPosition(double seconds)
{
    if (seconds != playheadSeconds_)
    {
        // Stripe repaint optimization - only repaint thin strips around old and new positions
        const int oldX = secondsToPixels(playheadSeconds_);
        const int newX = secondsToPixels(seconds);
        
        playheadSeconds_ = seconds;
        
        // Repaint only the affected regions
        const int stripeWidth = 7;
        repaint(oldX - stripeWidth, 0, stripeWidth * 2, getHeight());
        repaint(newX - stripeWidth, 0, stripeWidth * 2, getHeight());
    }
}

//==============================================================================
void VirtualTimelineComponent::selectTrack(TrackId id, bool addToSelection)
{
    if (!addToSelection)
        selectedTracks_.clear();
    
    selectedTracks_.insert(id);
    
    // Update render data
    for (auto& track : tracks_)
    {
        track.selected = selectedTracks_.count(track.id) > 0;
    }
    
    // Trigger repaint to show selection
    repaint();
}

void VirtualTimelineComponent::clearSelection()
{
    selectedTracks_.clear();
    for (auto& track : tracks_)
    {
        track.selected = false;
    }
    
    // Trigger repaint to clear selection
    repaint();
}

std::vector<VirtualTimelineComponent::TrackId> VirtualTimelineComponent::getSelectedTracks() const
{
    return std::vector<TrackId>(selectedTracks_.begin(), selectedTracks_.end());
}

std::vector<int> VirtualTimelineComponent::getSelectedDatabaseTrackIds() const
{
    std::vector<int> databaseTrackIds;
    
    for (const auto& internalId : selectedTracks_)
    {
        for (const auto& track : tracks_)
        {
            if (track.id == internalId && track.trackInfo)
            {
                databaseTrackIds.push_back(track.trackInfo->trackId);
                break;
            }
        }
    }
    
    return databaseTrackIds;
}

void VirtualTimelineComponent::showContextMenu(juce::Point<int> position)
{
    juce::PopupMenu menu;
    
    const bool hasSelection = !selectedTracks_.empty();
    const bool hasClipboard = !clipboardTracks_.empty();
    
    // Build the menu
    menu.addItem(1, "Cut", hasSelection);
    menu.addItem(2, "Copy", hasSelection);
    menu.addSeparator();
    menu.addItem(3, "Paste Before", hasClipboard && hasSelection);
    menu.addItem(4, "Paste After", hasClipboard && hasSelection);
    menu.addSeparator();
    menu.addItem(5, "Delete", hasSelection);
    menu.addItem(6, "Remove All Following Tracks", hasSelection);
    
    // Convert the local position to screen coordinates
    const auto screenPos = localPointToGlobal(position);
    
    // Show the menu and handle the result
    menu.showMenuAsync(juce::PopupMenu::Options()
                        .withTargetComponent(this)
                        .withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
                      [this](int result)
    {
        handleContextMenuResult(result);
    });
}

void VirtualTimelineComponent::handleContextMenuResult(int result)
{
    switch (result)
    {
        case 1: // Cut
            copySelectedTracks();
            deleteSelectedTracks();
            break;
            
        case 2: // Copy
            copySelectedTracks();
            break;
            
        case 3: // Paste Before
            pasteTracksBeforeSelection();
            break;
            
        case 4: // Paste After
            pasteTracksAfterSelection();
            break;
            
        case 5: // Delete
            deleteSelectedTracks();
            break;
            
        case 6: // Remove All Following Tracks
            removeAllFollowingTracks();
            break;
    }
}

void VirtualTimelineComponent::copySelectedTracks()
{
    spdlog::info("[COPY] copySelectedTracks() called - {} tracks selected", selectedTracks_.size());
    clipboardTracks_.clear();
    
    for (const auto& trackId : selectedTracks_)
    {
        spdlog::info("[COPY] Looking for track with ID {}", trackId);
        // Find the track data
        bool found = false;
        for (const auto& track : tracks_)
        {
            if (track.id == trackId && track.mixTrack)
            {
                // Store a copy of the MixTrack data
                clipboardTracks_.push_back(*track.mixTrack);
                spdlog::info("[COPY] Found and copied track {} (order {})", trackId, track.mixTrack->orderInMix);
                found = true;
                break;
            }
        }
        if (!found)
        {
            spdlog::warn("[COPY] Could not find track {} in tracks list", trackId);
        }
    }
    
    spdlog::info("[COPY] Copied {} tracks to clipboard (from {} selected)", clipboardTracks_.size(), selectedTracks_.size());
}

void VirtualTimelineComponent::deleteSelectedTracks()
{
    if (selectedTracks_.empty())
        return;
    
    // Collect the tracks to delete
    std::vector<int> ordersToDelete;
    for (const auto& trackId : selectedTracks_)
    {
        for (const auto& track : tracks_)
        {
            if (track.id == trackId && track.mixTrack)
            {
                ordersToDelete.push_back(track.mixTrack->orderInMix);
                break;
            }
        }
    }
    
    // Sort in reverse order so we delete from the end first
    std::sort(ordersToDelete.rbegin(), ordersToDelete.rend());
    
    // Delete tracks via the callback
    if (onDeleteTracksRequested)
    {
        onDeleteTracksRequested();
    }
    
    clearSelection();
}

void VirtualTimelineComponent::pasteTracksBeforeSelection()
{
    if (clipboardTracks_.empty() || selectedTracks_.empty())
        return;
    
    // Find the minimum orderInMix of selected tracks
    int minOrder = std::numeric_limits<int>::max();
    for (const auto& trackId : selectedTracks_)
    {
        for (const auto& track : tracks_)
        {
            if (track.id == trackId && track.mixTrack)
            {
                minOrder = std::min(minOrder, track.mixTrack->orderInMix);
                break;
            }
        }
    }
    
    // Notify parent to insert tracks before this position
    if (onPasteTracksRequested)
    {
        onPasteTracksRequested(clipboardTracks_, minOrder, true);
    }
}

void VirtualTimelineComponent::pasteTracksAfterSelection()
{
    spdlog::info("[PASTE] pasteTracksAfterSelection() called - clipboard: {} tracks, selection: {} tracks",
                clipboardTracks_.size(), selectedTracks_.size());
    
    if (clipboardTracks_.empty() || selectedTracks_.empty())
    {
        spdlog::warn("[PASTE] Cannot paste - clipboard empty: {}, no selection: {}",
                    clipboardTracks_.empty(), selectedTracks_.empty());
        return;
    }
    
    // Find the maximum orderInMix of selected tracks
    int maxOrder = -1;
    for (const auto& trackId : selectedTracks_)
    {
        for (const auto& track : tracks_)
        {
            if (track.id == trackId && track.mixTrack)
            {
                maxOrder = std::max(maxOrder, track.mixTrack->orderInMix);
                break;
            }
        }
    }
    
    // Notify parent to insert tracks after this position
    spdlog::info("[PASTE] Will paste {} tracks after position {}", clipboardTracks_.size(), maxOrder);
    
    if (onPasteTracksRequested)
    {
        spdlog::info("[PASTE] Calling onPasteTracksRequested callback");
        // Pass maxOrder directly since we're using orderInMix (0-based) as track IDs
        onPasteTracksRequested(clipboardTracks_, maxOrder, false);
        
        // After paste, select the newly pasted track(s)
        // The pasted tracks will be at positions maxOrder+1 through maxOrder+clipboardTracks_.size()
        clearSelection();
        for (size_t i = 0; i < clipboardTracks_.size(); ++i)
        {
            selectTrack(maxOrder + 1 + static_cast<int>(i), true);
        }
        spdlog::info("[PASTE] Selected newly pasted track(s) starting at position {}", maxOrder + 1);
    }
    else
    {
        spdlog::error("[PASTE] onPasteTracksRequested callback is not set!");
    }
}

void VirtualTimelineComponent::removeAllFollowingTracks()
{
    if (selectedTracks_.empty())
        return;
    
    // Find the minimum orderInMix of selected tracks
    int minOrder = std::numeric_limits<int>::max();
    for (const auto& trackId : selectedTracks_)
    {
        for (const auto& track : tracks_)
        {
            if (track.id == trackId && track.mixTrack)
            {
                minOrder = std::min(minOrder, track.mixTrack->orderInMix);
                break;
            }
        }
    }
    
    // Notify parent to remove all tracks after this position
    if (onRemoveFollowingTracksRequested)
    {
        onRemoveFollowingTracksRequested(minOrder);
    }
}

//==============================================================================
void VirtualTimelineComponent::calculateTrackPositions()
{
    if (tracks_.empty())
        return;
    
    // --- 1. Calculate component width ---
    double maxTimeSecs = 0.0;
    for (const auto& track : tracks_)
    {
        const double startTime = std::chrono::duration<double>(track.componentStartTime).count();
        const double endTime = startTime + track.effectiveDuration;
        maxTimeSecs = std::max(maxTimeSecs, endTime);
    }
    calculatedWidth_ = static_cast<int>(maxTimeSecs * pixelsPerSecond_) + 200; // Extra padding

    // --- 2. Calculate track layout and required height ---
    // Use viewport bounds height to determine available lanes
    const int availableHeightForLanes = (viewportBounds_.getHeight() > 0 ? viewportBounds_.getHeight() : 600) - rulerHeight;
    const int numLanes = std::max(1, availableHeightForLanes / (trackHeight + yGap));
    
    spdlog::info("calculateTrackPositions: viewport height: {}, available height: {}, numLanes: {}",
                 viewportBounds_.getHeight(), availableHeightForLanes, numLanes);
    
    int currentLane = 0;
    int laneDirection = 1;
    int maxYPos = 0;

    for (auto& track : tracks_)
    {
        track.laneIndex = currentLane;
        const int yPos = rulerHeight + (currentLane * (trackHeight + yGap));
        maxYPos = std::max(maxYPos, yPos);
        
        const double startTime = std::chrono::duration<double>(track.componentStartTime).count();
        const int startX = static_cast<int>(startTime * pixelsPerSecond_);
        const int width = static_cast<int>(track.effectiveDuration * pixelsPerSecond_);
        
        track.bounds = juce::Rectangle<int>(startX, yPos, width, trackHeight);
        track.waveformBounds = track.bounds.reduced(waveformInset);
        
        if (numLanes > 1) {
            if ((currentLane + laneDirection) >= numLanes || (currentLane + laneDirection) < 0)
                laneDirection *= -1;
            currentLane += laneDirection;
        }
    }
    
    calculatedHeight_ = maxYPos + trackHeight + yGap;
    
    // Ensure component is at least as tall as the viewport to allow for expansion
    if (viewportBounds_.getHeight() > 0)
    {
        calculatedHeight_ = std::max(calculatedHeight_, viewportBounds_.getHeight());
    }

    // --- 3. Set component size ---
    if (getWidth() != calculatedWidth_ || getHeight() != calculatedHeight_)
    {
        setSize(calculatedWidth_, calculatedHeight_);
    }
    else
    {
        refreshLayout();
    }
}

void VirtualTimelineComponent::refreshLayout()
{
    // Position calculation is now done in calculateTrackPositions.
    // This function is now only for updating what's visible.
    updateVisibleTracks();
}

void VirtualTimelineComponent::updateVisibleTracks()
{
    const auto visibleArea = getVisibleArea();
    int visibleCount = 0;
    
    spdlog::info("[VIRTUAL_TIMELINE] Updating visible tracks - visible area: x={}, y={}, w={}, h={}",
                visibleArea.getX(), visibleArea.getY(), visibleArea.getWidth(), visibleArea.getHeight());
    
    for (auto& track : tracks_)
    {
        track.isVisible = track.bounds.intersects(visibleArea);
        if (track.isVisible)
        {
            visibleCount++;
            if (visibleCount <= 5)
            {
                spdlog::info("[VIRTUAL_TIMELINE]   Track {} is visible (bounds: x={}, y={}, w={}, h={})",
                            track.id, track.bounds.getX(), track.bounds.getY(), 
                            track.bounds.getWidth(), track.bounds.getHeight());
            }
        }
    }
    
    spdlog::info("[VIRTUAL_TIMELINE] {} tracks visible out of {} total", visibleCount, tracks_.size());
    metrics_.visibleTracks = visibleCount;
}

VirtualTimelineComponent::TrackRenderData* VirtualTimelineComponent::getTrackAt(juce::Point<int> point)
{
    // Check all tracks for hit-testing (since they can overlap in time-based layout)
    for (auto& track : tracks_)
    {
        if (track.bounds.contains(point))
            return &track;
    }
    return nullptr;
}

VirtualTimelineComponent::HitTestInfo VirtualTimelineComponent::hitTest(juce::Point<int> point) const
{
    HitTestInfo info;
    
    // First check if we're over a track
    for (auto& track : tracks_)
    {
        if (track.bounds.contains(point))
        {
            info.track = const_cast<TrackRenderData*>(&track);
            info.type = HitTestResult::Track;
            
            if (track.mixTrack)
            {
                // Check for cue points and attach points (within 8 pixels)
                const int hitRadius = 8;
                
                // Check cue start (left edge of track)
                if (std::abs(point.x - track.bounds.getX()) < hitRadius)
                {
                    info.type = HitTestResult::CueStart;
                    return info;
                }
                
                // Check cue end (right edge of track)  
                if (std::abs(point.x - track.bounds.getRight()) < hitRadius)
                {
                    info.type = HitTestResult::CueEnd;
                    return info;
                }
                
                // AttachFrom point
                const double attachFromTime = std::chrono::duration<double>(
                    track.audioStartTime + track.mixTrack->attachFrom).count();
                const int attachFromX = track.bounds.getX() + static_cast<int>(
                    (attachFromTime - std::chrono::duration<double>(track.componentStartTime).count()) 
                    * pixelsPerSecond_);
                
                if (std::abs(point.x - attachFromX) < hitRadius)
                {
                    info.type = HitTestResult::AttachFrom;
                    return info;
                }
                
                // AttachTo point
                const double attachToTime = std::chrono::duration<double>(
                    track.audioStartTime + track.mixTrack->attachTo).count();
                const int attachToX = track.bounds.getX() + static_cast<int>(
                    (attachToTime - std::chrono::duration<double>(track.componentStartTime).count()) 
                    * pixelsPerSecond_);
                
                if (std::abs(point.x - attachToX) < hitRadius)
                {
                    info.type = HitTestResult::AttachTo;
                    return info;
                }
                
                // Check envelope points
                int pointIndex = 0;
                for (const auto& envPoint : track.mixTrack->envelopePoints)
                {
                    const double pointTimeInComponent = std::chrono::duration<double>(
                        track.audioStartTime + envPoint.time).count();
                    const int x = track.bounds.getX() + static_cast<int>(
                        (pointTimeInComponent - std::chrono::duration<double>(track.componentStartTime).count()) 
                        * pixelsPerSecond_);
                    
                    const float normalizedVolume = static_cast<float>(envPoint.volume) / 1000.0f;
                    const int y = track.waveformBounds.getY() + 
                                 static_cast<int>((1.0f - normalizedVolume) * track.waveformBounds.getHeight());
                    
                    if (point.getDistanceFrom(juce::Point<int>(x, y)) < hitRadius)
                    {
                        info.type = HitTestResult::EnvelopePoint;
                        info.pointIndex = pointIndex;
                        return info;
                    }
                    pointIndex++;
                }
            }
            
            return info;
        }
    }
    
    return info;
}

int VirtualTimelineComponent::secondsToPixels(double seconds) const
{
    return static_cast<int>(seconds * pixelsPerSecond_);
}

double VirtualTimelineComponent::pixelsToSeconds(int pixels) const
{
    return pixels / pixelsPerSecond_;
}

juce::Rectangle<int> VirtualTimelineComponent::getVisibleArea() const
{
    if (viewportBounds_.isEmpty())
        return getLocalBounds();
    
    // Convert viewport bounds to our coordinate space
    return juce::Rectangle<int>(0, viewportBounds_.getY(), 
                                getWidth(), viewportBounds_.getHeight());
}

//==============================================================================
void VirtualTimelineComponent::paintTracks(juce::Graphics& g)
{
    for (const auto& track : tracks_)
    {
        if (!track.isVisible)
            continue;
        
        paintTrack(g, track);
    }
}

void VirtualTimelineComponent::paintTrack(juce::Graphics& g, const TrackRenderData& track)
{
    // Log what we're painting for debugging
    if (track.mixTrack)
    {
        spdlog::info("[PAINT] Painting track id={}, trackId={}, orderInMix={}, bounds=({},{},{},{}), name={}",
                    track.id, track.mixTrack->trackId, track.mixTrack->orderInMix,
                    track.bounds.getX(), track.bounds.getY(), 
                    track.bounds.getWidth(), track.bounds.getHeight(),
                    track.name.toStdString());
    }
    
    // Track background - make it subtly different from the main editor background
    const auto& lf = getLookAndFeel();
    auto baseColour = lf.findColour(juce::TextEditor::backgroundColourId);
    
    // Determine if we're in a dark or light theme
    const bool isDarkTheme = baseColour.getBrightness() < 0.5f;
    
    if (track.selected)
    {
        // Selected track - more prominent with accent color tint
        auto selectedColour = baseColour;
        // Add subtle accent color tint and make it brighter/darker based on theme
        auto accentColour = lf.findColour(juce::TextButton::buttonColourId);
        selectedColour = selectedColour.interpolatedWith(accentColour, 0.08f);
        
        if (isDarkTheme)
            selectedColour = selectedColour.brighter(0.15f);
        else
            selectedColour = selectedColour.darker(0.06f);
            
        g.setColour(selectedColour);
        g.fillRoundedRectangle(track.bounds.toFloat(), 4.0f);
    }
    else
    {
        // Non-selected track - subtly different from main background for visibility
        auto trackColour = baseColour;
        
        if (isDarkTheme)
        {
            // Dark theme: make tracks slightly brighter with very subtle warmth
            trackColour = trackColour.brighter(0.06f);
            // Add tiny bit of warmth to distinguish from pure grey
            trackColour = trackColour.interpolatedWith(juce::Colour(255, 248, 240), 0.02f);
        }
        else
        {
            // Light theme: make tracks slightly darker/greyer
            trackColour = trackColour.darker(0.03f);
            // Add tiny bit of coolness to distinguish from pure white
            trackColour = trackColour.interpolatedWith(juce::Colour(240, 243, 248), 0.04f);
        }
        
        // Apply with very slight transparency for depth
        g.setColour(trackColour.withAlpha(0.97f));
        g.fillRoundedRectangle(track.bounds.toFloat(), 4.0f);
        
        // Subtle border (only for non-selected tracks) - slightly more visible
        g.setColour(lf.findColour(juce::TextEditor::outlineColourId).withAlpha(0.25f));
        g.drawRoundedRectangle(track.bounds.toFloat(), 4.0f, 0.7f);
    }
    
    // Paint waveform using tiles
    if (track.thumbnail && track.thumbnail->isFullyLoaded())
    {
        // Add debug logging to see if we get here
        static int waveformPaintCount = 0;
        if (++waveformPaintCount % 100 == 0)
        {
            spdlog::info("Painting waveform for track {}, has thumbnail: {}, fully loaded: {}", 
                        track.id, 
                        track.thumbnail ? "yes" : "no",
                        track.thumbnail ? (track.thumbnail->isFullyLoaded() ? "yes" : "no") : "n/a");
        }
        // Get the visible portion of this track
        const auto visibleArea = getVisibleArea();
        const auto trackVisibleBounds = track.bounds.getIntersection(visibleArea);
        
        if (!trackVisibleBounds.isEmpty())
        {
            // Save graphics state and clip to waveform bounds
            juce::Graphics::ScopedSaveState state(g);
            g.reduceClipRegion(track.waveformBounds);
            
            // Get all tile keys needed for this track's visible area
            const auto tileKeys = getTileKeysForTrack(track, trackVisibleBounds);
            
            // Log if we're getting tile keys
            if (tileKeys.empty())
            {
                static int emptyCount = 0;
                if (++emptyCount % 20 == 0)
                {
                    spdlog::info("[TILE_KEYS] Track {} has NO tile keys for visible area", track.id);
                }
            }
            
            bool allTilesReady = false;  // Start with false, set to true only if all tiles are ready
            int tilesReady = 0;
            int tilesNotReady = 0;
            
            // Try to paint using cached tiles
            if (!tileKeys.empty())
            {
                allTilesReady = true; // Assume true, will be set false if any tile is missing
                for (const auto& key : tileKeys)
                {
                    if (auto tile = tileCache_->getTile(key))
                    {
                        if (tile->isReady)
                        {
                            tilesReady++;
                            
                            // Calculate where this tile should be drawn
                            const auto destRect = getTileDestinationRect(track, key.startTimeSeconds, key.endTimeSeconds);
                            
                            // Draw the tile image
                            static int drawCount = 0;
                            if (++drawCount % 100 == 0)
                            {
                                spdlog::info("[TILE_DRAW] Drawing tile at rect: x={}, y={}, w={}, h={}, image size: {}x{}",
                                           destRect.getX(), destRect.getY(), destRect.getWidth(), destRect.getHeight(),
                                           tile->image.getWidth(), tile->image.getHeight());
                            }
                            
                            // Draw the tile image
                            g.setOpacity(1.0f);
                            g.drawImage(tile->image, destRect.toFloat(),
                                       juce::RectanglePlacement::stretchToFit);
                        }
                        else
                        {
                            tilesNotReady++;
                            allTilesReady = false;
                        }
                    }
                    else
                    {
                        tilesNotReady++;
                        allTilesReady = false;
                    }
                }
            }
            
            // Log tile statistics periodically
            static int tileStatsCount = 0;
            if (++tileStatsCount % 50 == 0)
            {
                spdlog::info("[TILE_STATS] Track {} - Keys: {}, Ready: {}, NotReady: {}, AllReady: {}", 
                            track.id, tileKeys.size(), tilesReady, tilesNotReady, allTilesReady);
            }
            
            // If not all tiles are ready, fill in missing parts with direct rendering
            if (!allTilesReady && tilesReady == 0)
            {
                // Only do full direct render if NO tiles are ready
                // Use waveform color from theme (matching tile rendering)
                const auto& lf = getLookAndFeel();
                
                // Use accent color for selected tracks, normal color otherwise
                if (track.selected)
                {
                    g.setColour(lf.findColour(juce::TextButton::buttonOnColourId).withAlpha(0.7f));
                }
                else
                {
                    g.setColour(lf.findColour(jucyaudio::ui::waveformColourId).withAlpha(0.7f));
                }
                
                // Calculate time range for thumbnail drawing
                double thumbnailStartTime = 0.0;
                double thumbnailEndTime = track.effectiveDuration;
                
                if (track.mixTrack && track.trackInfo)
                {
                    thumbnailStartTime = std::chrono::duration<double>(
                        std::max(jucyaudio::Duration_t{0}, track.mixTrack->cueStart)).count();
                    thumbnailEndTime = std::chrono::duration<double>(track.trackInfo->duration).count();
                }
                else if (track.trackInfo)
                {
                    thumbnailEndTime = std::chrono::duration<double>(track.trackInfo->duration).count();
                }

                const bool drawStereo = config::theSettings.mixEditingSettings.drawStereoWaveforms.get();

                if (drawStereo && track.thumbnail->getNumChannels() > 1)
                {
                    auto topHalf = track.waveformBounds.withHeight(track.waveformBounds.getHeight() / 2);
                    auto bottomHalf = track.waveformBounds.withY(topHalf.getBottom());
                    track.thumbnail->drawChannel(g, topHalf, thumbnailStartTime, thumbnailEndTime, 0, 1.0f);
                    track.thumbnail->drawChannel(g, bottomHalf, thumbnailStartTime, thumbnailEndTime, 1, 1.0f);
                }
                else
                {
                    track.thumbnail->drawChannel(g, track.waveformBounds, thumbnailStartTime, thumbnailEndTime, 0, 1.0f);
                }
            }
            // If some tiles are ready, the missing ones will be queued and filled in soon
        }
    }
    else
    {
        // Fallback: placeholder for tracks without a thumbnail or if it's still loading
        static int fallbackPaintCount = 0;
        if (++fallbackPaintCount % 50 == 0)
        {
            spdlog::info("Using fallback for track {}, has thumbnail: {}, fully loaded: {}", 
                        track.id, 
                        track.thumbnail ? "yes" : "no",
                        track.thumbnail ? (track.thumbnail->isFullyLoaded() ? "yes" : "no") : "n/a");
        }
        
        g.setColour(juce::Colours::darkgrey.withAlpha(0.3f));
        g.fillRect(track.waveformBounds);
        g.setColour(juce::Colours::black.withAlpha(0.2f));
        g.drawRect(track.waveformBounds);
    }
    
    // Apply accent color overlay to waveform area if track is selected
    if (track.selected)
    {
        const auto& lf = getLookAndFeel();
        g.setColour(lf.findColour(juce::TextButton::buttonOnColourId).withAlpha(0.2f));
        g.fillRect(track.waveformBounds);
    }
    
    // Track info text
    g.setColour(getLookAndFeel().findColour(juce::Label::textColourId));
    g.setFont(12.0f);
    
    // Build track label with all info
    juce::String trackLabel;
    
    // Add artist, album, and title
    if (track.trackInfo)
    {
        // Artist name
        if (!track.trackInfo->artist_name.empty())
        {
            trackLabel += juce::String(track.trackInfo->artist_name);
        }
        
        // Album title
        if (!track.trackInfo->album_title.empty())
        {
            if (!trackLabel.isEmpty()) trackLabel += " - ";
            trackLabel += juce::String(track.trackInfo->album_title);
        }
        
        // Track title
        if (!trackLabel.isEmpty()) trackLabel += " - ";
        trackLabel += juce::String(track.trackInfo->title);
        
        // Duration info
        const int totalSeconds = static_cast<int>(track.effectiveDuration);
        const int mins = totalSeconds / 60;
        const int secs = totalSeconds % 60;
        trackLabel += juce::String::formatted(" [%d:%02d]", mins, secs);
        
        // BPM if available (stored as integer * 100, so divide by 100)
        if (track.trackInfo->bpm.has_value() && track.trackInfo->bpm.value() > 0)
        {
            const double bpmValue = track.trackInfo->bpm.value() / 100.0;
            trackLabel += juce::String::formatted(" %.1fbpm", bpmValue);
        }
    }
    else
    {
        // Fallback to just the name if no track info
        trackLabel = track.name;
    }
    
    // Draw label text at the top-left of the track
    const auto labelBounds = juce::Rectangle<int>(
        track.bounds.getX() + 4,
        track.bounds.getY() + 2,
        track.bounds.getWidth() - 8,
        20
    );
    g.drawText(trackLabel, labelBounds, juce::Justification::topLeft, true);
    
    // --- 4.1.5 Crossfade Visualization ---
    
    // Log envelope availability
    static int envelopeCheckCount = 0;
    if (++envelopeCheckCount % 50 == 0)
    {
        spdlog::info("[ENVELOPE_CHECK] Track {}: mixTrack={}, envelopePoints={}", 
                    track.id, 
                    track.mixTrack ? "yes" : "no",
                    track.mixTrack ? track.mixTrack->envelopePoints.size() : 0);
    }
    
    // Draw envelope if we have envelope points
    if (track.mixTrack && !track.mixTrack->envelopePoints.empty())
    {
        // Log envelope information
        static int envelopeLogCount = 0;
        if (++envelopeLogCount % 10 == 0)  // More frequent logging
        {
            spdlog::info("[ENVELOPE] Track {} has {} envelope points", 
                        track.id, track.mixTrack->envelopePoints.size());
            for (size_t i = 0; i < std::min(size_t(3), track.mixTrack->envelopePoints.size()); ++i)
            {
                const auto& pt = track.mixTrack->envelopePoints[i];
                spdlog::info("[ENVELOPE]   Point {}: time={:.3f}s, volume={:.3f}", 
                            i, std::chrono::duration<double>(pt.time).count(), pt.volume);
            }
        }
        
        juce::Path envelopePath;
        bool firstPoint = true;
        
        int pointIndex = 0;
        for (const auto& point : track.mixTrack->envelopePoints)
        {
            // Convert envelope time (relative to audio start) to pixels
            const double pointTimeInComponent = std::chrono::duration<double>(
                track.audioStartTime + point.time).count();
            const int x = track.bounds.getX() + static_cast<int>(
                (pointTimeInComponent - std::chrono::duration<double>(track.componentStartTime).count()) 
                * pixelsPerSecond_);
            
            // Volume: 1.0 = top of waveform area, 0.0 = bottom
            // Volume_t is stored as integer where 1000 = 100% (1.0)
            const float normalizedVolume = std::max(0.0f, std::min(1.0f, static_cast<float>(point.volume) / 1000.0f));
            const int y = track.waveformBounds.getY() + 
                         static_cast<int>((1.0f - normalizedVolume) * track.waveformBounds.getHeight());
            
            // Log first few points' coordinates
            if (pointIndex < 3 && envelopeLogCount % 10 == 1)
            {
                spdlog::info("[ENVELOPE_COORD] Point {}: volume={:.3f}, normalized={:.3f}, x={}, y={} (bounds: x={}-{}, y={}-{})",
                            pointIndex, point.volume, normalizedVolume, x, y, 
                            track.bounds.getX(), track.bounds.getRight(),
                            track.waveformBounds.getY(), track.waveformBounds.getBottom());
            }
            pointIndex++;
            
            if (firstPoint)
            {
                envelopePath.startNewSubPath(static_cast<float>(x), static_cast<float>(y));
                firstPoint = false;
            }
            else
            {
                envelopePath.lineTo(static_cast<float>(x), static_cast<float>(y));
            }
        }
        
        // Draw the envelope curve with a more visible color for testing
        g.setColour(juce::Colours::red.withAlpha(0.9f));  // Changed to red for visibility
        g.strokePath(envelopePath, juce::PathStrokeType(3.0f));  // Thicker line
        
        // Draw envelope points as white circles for visibility
        g.setColour(juce::Colours::white);  // Changed to white for visibility
        for (const auto& point : track.mixTrack->envelopePoints)
        {
            const double pointTimeInComponent = std::chrono::duration<double>(
                track.audioStartTime + point.time).count();
            const int x = track.bounds.getX() + static_cast<int>(
                (pointTimeInComponent - std::chrono::duration<double>(track.componentStartTime).count()) 
                * pixelsPerSecond_);
            // Volume_t is stored as integer where 1000 = 100% (1.0)
            const float normalizedVolume = std::max(0.0f, std::min(1.0f, static_cast<float>(point.volume) / 1000.0f));
            const int y = track.waveformBounds.getY() + 
                         static_cast<int>((1.0f - normalizedVolume) * track.waveformBounds.getHeight());
            
            const float pointRadius = 6.0f;  // Larger radius for visibility
            // Draw white circle with black outline
            g.setColour(juce::Colours::black);
            g.drawEllipse(static_cast<float>(x) - pointRadius, static_cast<float>(y) - pointRadius, 
                         pointRadius * 2.0f, pointRadius * 2.0f, 2.0f);
            g.setColour(juce::Colours::white);
            g.fillEllipse(static_cast<float>(x) - pointRadius, static_cast<float>(y) - pointRadius, 
                         pointRadius * 2.0f, pointRadius * 2.0f);
        }
    }
    
    // No visual cue point markers needed - the track edges themselves are the cue points
    // Draw attach points with more visible color
    if (track.mixTrack)
    {
        const auto attachColor = juce::Colours::cyan;  // Changed to cyan for visibility
        g.setColour(attachColor);
        
        // AttachFrom point (where this track attaches from the previous)
        if (track.mixTrack->attachFrom.count() > 0)
        {
            const double attachFromTime = std::chrono::duration<double>(
                track.audioStartTime + track.mixTrack->attachFrom).count();
            const int attachFromX = track.bounds.getX() + static_cast<int>(
                (attachFromTime - std::chrono::duration<double>(track.componentStartTime).count()) 
                * pixelsPerSecond_);
            
            // Draw attach point as a vertical line with diamond
            g.drawVerticalLine(attachFromX, 
                              static_cast<float>(track.bounds.getY()), 
                              static_cast<float>(track.bounds.getBottom()));
            
            // Draw diamond marker
            juce::Path diamond;
            diamond.addQuadrilateral(static_cast<float>(attachFromX - 4), static_cast<float>(track.bounds.getCentreY()),
                                    static_cast<float>(attachFromX), static_cast<float>(track.bounds.getCentreY() - 4),
                                    static_cast<float>(attachFromX + 4), static_cast<float>(track.bounds.getCentreY()),
                                    static_cast<float>(attachFromX), static_cast<float>(track.bounds.getCentreY() + 4));
            g.fillPath(diamond);
        }
        
        // AttachTo point (where the next track will attach)
        if (track.mixTrack->attachTo.count() > 0)
        {
            const double attachToTime = std::chrono::duration<double>(
                track.audioStartTime + track.mixTrack->attachTo).count();
            const int attachToX = track.bounds.getX() + static_cast<int>(
                (attachToTime - std::chrono::duration<double>(track.componentStartTime).count()) 
                * pixelsPerSecond_);
            
            // Draw attach point as a vertical line with circle
            g.drawVerticalLine(attachToX, 
                              static_cast<float>(track.bounds.getY()), 
                              static_cast<float>(track.bounds.getBottom()));
            
            // Draw circle marker
            g.fillEllipse(static_cast<float>(attachToX - 4), 
                         static_cast<float>(track.bounds.getCentreY() - 4), 
                         8.0f, 8.0f);
        }
    }
}

void VirtualTimelineComponent::paintGrid(juce::Graphics& g)
{
    const auto clipBounds = g.getClipBounds();
    
    // --- 1. Draw ruler background ---
    g.setColour(juce::Colours::darkgrey.withAlpha(0.3f));
    g.fillRect(0, 0, clipBounds.getRight(), rulerHeight);
    
    // Draw ruler bottom border
    g.setColour(juce::Colours::grey);
    g.drawHorizontalLine(rulerHeight - 1, 0.0f, static_cast<float>(clipBounds.getRight()));
    
    // --- 2. Calculate grid spacing based on zoom level ---
    double gridIntervalSeconds;
    if (pixelsPerSecond_ < 5)
        gridIntervalSeconds = 60.0;  // 1 minute intervals when very zoomed out
    else if (pixelsPerSecond_ < 20)
        gridIntervalSeconds = 10.0;  // 10 second intervals
    else if (pixelsPerSecond_ < 100)
        gridIntervalSeconds = 1.0;   // 1 second intervals
    else
        gridIntervalSeconds = 0.1;   // 100ms intervals when very zoomed in
    
    const int pixelsPerGridLine = static_cast<int>(gridIntervalSeconds * pixelsPerSecond_);
    
    // --- 3. Draw grid lines and time labels ---
    if (pixelsPerGridLine > 5) // Only draw if grid lines are spaced enough
    {
        // Calculate first visible grid line
        const int firstVisiblePixel = clipBounds.getX();
        const double firstVisibleSecond = pixelsToSeconds(firstVisiblePixel);
        const int firstGridIndex = static_cast<int>(firstVisibleSecond / gridIntervalSeconds);
        const double firstGridSecond = firstGridIndex * gridIntervalSeconds;
        const int firstGridPixel = secondsToPixels(firstGridSecond);
        
        // Draw vertical grid lines and time labels
        g.setFont(10.0f);
        for (int x = firstGridPixel; x <= clipBounds.getRight(); x += pixelsPerGridLine)
        {
            if (x < 0) continue;
            
            const double timeSeconds = pixelsToSeconds(x);
            const int minutes = static_cast<int>(timeSeconds) / 60;
            const int seconds = static_cast<int>(timeSeconds) % 60;
            const int milliseconds = static_cast<int>((timeSeconds - static_cast<int>(timeSeconds)) * 1000);
            
            // Draw major grid lines (every minute or every 10 seconds)
            const bool isMajorLine = (gridIntervalSeconds >= 10.0 && static_cast<int>(timeSeconds) % 60 == 0) ||
                                    (gridIntervalSeconds < 10.0 && static_cast<int>(timeSeconds * 10) % 100 == 0);
            
            if (isMajorLine)
            {
                // Major grid line
                g.setColour(juce::Colours::grey.withAlpha(0.5f));
                g.drawVerticalLine(x, static_cast<float>(rulerHeight), static_cast<float>(clipBounds.getBottom()));
                
                // Ruler tick
                g.drawLine(static_cast<float>(x), static_cast<float>(rulerHeight - 10), 
                          static_cast<float>(x), static_cast<float>(rulerHeight), 1.0f);
                
                // Time label
                juce::String timeStr;
                if (gridIntervalSeconds >= 1.0)
                    timeStr = juce::String::formatted("%d:%02d", minutes, seconds);
                else
                    timeStr = juce::String::formatted("%d:%02d.%03d", minutes, seconds, milliseconds);
                
                g.setColour(getLookAndFeel().findColour(juce::Label::textColourId));
                g.setFont(10.0f);
                g.drawText(timeStr, x - 30, 5, 60, 20, juce::Justification::centred);
            }
            else
            {
                // Minor grid line
                g.setColour(juce::Colours::grey.withAlpha(0.2f));
                g.drawVerticalLine(x, static_cast<float>(rulerHeight), static_cast<float>(clipBounds.getBottom()));
                
                // Ruler tick
                g.setColour(juce::Colours::grey.withAlpha(0.5f));
                g.drawLine(static_cast<float>(x), static_cast<float>(rulerHeight - 5), 
                          static_cast<float>(x), static_cast<float>(rulerHeight), 0.5f);
            }
        }
    }
}

void VirtualTimelineComponent::paintPlayhead(juce::Graphics& g)
{
    // Draw clicked position (accent color line) - stays visible after click
    if (clickedTimePosition_ >= 0.0)
    {
        const int clickX = secondsToPixels(clickedTimePosition_);
        if (clickX >= 0 && clickX < getWidth())
        {
            // Use the theme's accent color (orange)
            const auto& lf = getLookAndFeel();
            g.setColour(lf.findColour(juce::TextButton::buttonOnColourId).withAlpha(0.8f));
            g.drawVerticalLine(clickX, 0.0f, static_cast<float>(getHeight()));
        }
    }
    
    // Draw playback position (red line) - only visible during playback
    if (playheadSeconds_ >= 0.0)
    {
        const int playheadX = secondsToPixels(playheadSeconds_);
        
        if (playheadX >= 0 && playheadX < getWidth())
        {
            g.setColour(juce::Colours::red);
            g.drawVerticalLine(playheadX, 0.0f, static_cast<float>(getHeight()));
            
            // Draw playhead triangle at top
            juce::Path triangle;
            triangle.addTriangle(static_cast<float>(playheadX - 5), 0.0f,
                               static_cast<float>(playheadX + 5), 0.0f,
                               static_cast<float>(playheadX), 10.0f);
            g.fillPath(triangle);
        }
    }
}

//==============================================================================
void VirtualTimelineComponent::runPerfHarness(int numTracks)
{
    spdlog::info("=== Virtual Timeline Performance Harness ===");
    spdlog::info("Creating {} synthetic tracks for testing", numTracks);
    
    // Clear existing tracks
    tracks_.clear();
    selectedTracks_.clear();
    
    // Generate synthetic tracks
    tracks_.reserve(numTracks);
    for (int i = 0; i < numTracks; ++i)
    {
        TrackRenderData track;
        track.id = i;
        track.name = juce::String::formatted("Track %d", i + 1);
        track.colour = juce::Colour();  // Not used - we'll use theme colors
        // rowIndex no longer exists - tracks use laneIndex now
        
        // Simulate mix track data
        track.mixTrack = std::make_shared<database::MixTrack>();
        track.mixTrack->mixId = 1;
        track.mixTrack->trackId = i;
        track.mixTrack->orderInMix = i;
        track.mixTrack->cueStart = jucyaudio::Duration_t{0};
        track.mixTrack->cueEnd = jucyaudio::Duration_t{180000}; // 3 minute tracks in milliseconds
        
        tracks_.push_back(std::move(track));
    }
    
    metrics_.totalTracks = numTracks;
    calculateTrackPositions();
    
    // Simulate scrolling and resizing
    spdlog::info("Starting performance test...");
    
    // Test 1: Measure paint performance during resize
    const auto startResize = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10; ++i)
    {
        setSize(getWidth() + 10, getHeight());
        setSize(getWidth() - 10, getHeight());
    }
    const auto endResize = std::chrono::high_resolution_clock::now();
    const auto resizeDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endResize - startResize);
    
    // Test 2: Measure scroll performance
    const auto startScroll = std::chrono::high_resolution_clock::now();
    for (int y = 0; y < getHeight(); y += 100)
    {
        setViewportBounds(juce::Rectangle<int>(0, y, getWidth(), 600));
        repaint();
    }
    const auto endScroll = std::chrono::high_resolution_clock::now();
    const auto scrollDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endScroll - startScroll);
    
    // Test 3: Measure playhead update performance
    const auto startPlayhead = std::chrono::high_resolution_clock::now();
    for (double t = 0.0; t < 10.0; t += 0.1)
    {
        setPlayheadPosition(t);
    }
    const auto endPlayhead = std::chrono::high_resolution_clock::now();
    const auto playheadDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endPlayhead - startPlayhead);
    
    // Report results
    spdlog::info("=== Performance Test Results ===");
    spdlog::info("Track count: {}", numTracks);
    spdlog::info("Resize test (20 operations): {} ms", resizeDuration.count());
    spdlog::info("Scroll test: {} ms", scrollDuration.count());
    spdlog::info("Playhead test (100 updates): {} ms", playheadDuration.count());
    spdlog::info("Average paint time: {:.2f} ms", metrics_.avgPaintTimeMs.load());
    spdlog::info("Paints per second: {}", metrics_.paintsPerSecond.load());
    spdlog::info("================================");
}

//==============================================================================
// Tiling System Implementation
//==============================================================================

int VirtualTimelineComponent::getZoomLevel() const
{
    if (pixelsPerSecond_ <= 0.0) 
        return 0;
    
    // Define zoom buckets based on pixels per second
    // Higher bucket = more zoomed out = less detail needed
    if (pixelsPerSecond_ >= 1000.0)  // Very zoomed in (> 1000 px/sec)
        return 0;  // Maximum detail
    else if (pixelsPerSecond_ >= 200.0)  // Zoomed in (200-1000 px/sec)
        return 1;  // High detail
    else if (pixelsPerSecond_ >= 50.0)   // Medium zoom (50-200 px/sec)
        return 2;  // Medium detail
    else if (pixelsPerSecond_ >= 10.0)   // Zoomed out (10-50 px/sec)
        return 3;  // Low detail
    else  // Very zoomed out (< 10 px/sec)
        return 4;  // Minimum detail
}

void VirtualTimelineComponent::queueVisibleTiles()
{
    if (!mixProjectLoader_)
        return;

    const auto visibleBounds = getVisibleArea();
    
    // Add prefetch margin for smooth scrolling
    const int prefetchMargin = tileWidth_ * 2;  // Prefetch 2 tiles on each side
    auto prefetchBounds = visibleBounds.expanded(prefetchMargin, 0);

    for (const auto& track : tracks_)
    {
        // Check if track intersects with prefetch area
        if (!track.bounds.intersects(prefetchBounds) || !track.thumbnail || !track.trackInfo)
            continue;

        queueTilesForTrack(track, prefetchBounds);
    }
}

void VirtualTimelineComponent::queueTilesForTrack(const TrackRenderData& track, const juce::Rectangle<int>& visibleArea)
{
    const auto tileKeys = getTileKeysForTrack(track, visibleArea);
    
    for (const auto& key : tileKeys)
    {
        // Check if tile is already in cache
        if (!tileCache_->getTile(key))
        {
            // Create render request for this tile
            const int tileHeight = track.waveformBounds.getHeight();
            juce::Rectangle<int> tileBounds(0, 0, tileWidth_, tileHeight);
            
            // Log only occasionally
            static int queueCount = 0;
            if (++queueCount % 100 == 0)
            {
                spdlog::info("[TILE_QUEUE] Track {} queuing tile: idx={}, time={:.2f}-{:.2f}", 
                             track.id, key.tileIndex, key.startTimeSeconds, key.endTimeSeconds);
            }
            
            tileRenderer_->requestTile({
                key,
                track.thumbnail,
                tileBounds,
                key.startTimeSeconds,
                key.endTimeSeconds,
                track.colour,
                0.0,  // Not used anymore since we pass exact times
                0.0   // Not used anymore
            });
        }
    }
}

std::vector<VirtualTimelineComponent::WaveformKey> 
VirtualTimelineComponent::getTileKeysForTrack(const TrackRenderData& track, const juce::Rectangle<int>& visibleArea) const
{
    std::vector<WaveformKey> keys;
    
    // Get the intersection of track bounds with visible area
    const auto trackVisibleBounds = track.bounds.getIntersection(visibleArea);
    if (trackVisibleBounds.isEmpty())
        return keys;
    
    // Get the actual cueStart (can be negative if adding silence before)
    const double cueStartTime = std::chrono::duration<double>(track.mixTrack->cueStart).count();
    const double trackDuration = std::chrono::duration<double>(track.trackInfo->duration).count();
    
    // CRITICAL: Tile indices must be based on a FIXED time grid, independent of zoom!
    // Each tile represents a fixed duration of audio
    // Larger tiles = fewer cache entries needed, but less granular updates
    constexpr double FIXED_SECONDS_PER_TILE = 30.0;  // Each tile = 30 seconds of audio (was 10)
    
    // Tiles are always based on actual audio time (0 to trackDuration)
    // We don't tile silence - only the actual audio content
    const int firstTileIdx = 0;  // Always start from beginning of actual audio
    const int lastTileIdx = static_cast<int>(std::ceil(trackDuration / FIXED_SECONDS_PER_TILE));
    
    // Get current settings
    const bool drawStereo = config::theSettings.mixEditingSettings.drawStereoWaveforms.get();
    
    // Generate keys for all tiles that might be visible
    for (int tileIdx = firstTileIdx; tileIdx <= lastTileIdx; ++tileIdx)
    {
        const double tileStartTime = tileIdx * FIXED_SECONDS_PER_TILE;
        const double tileEndTime = (tileIdx + 1) * FIXED_SECONDS_PER_TILE;
        
        // Clip to actual track bounds (audio time, not component time)
        const double actualStart = std::max(0.0, tileStartTime);
        const double actualEnd = std::min(trackDuration, tileEndTime);
        
        if (actualStart < actualEnd)
        {
            // Check if this tile is actually visible on screen
            // When cueStart is negative, audio starts later in the component
            const double audioOffsetInComponent = std::max(0.0, -cueStartTime);
            const double componentTileStart = actualStart + audioOffsetInComponent + std::chrono::duration<double>(track.componentStartTime).count();
            const int tilePixelStart = secondsToPixels(componentTileStart);
            const int tilePixelEnd = secondsToPixels(componentTileStart + (actualEnd - actualStart));
            
            if (tilePixelEnd >= trackVisibleBounds.getX() && tilePixelStart <= trackVisibleBounds.getRight())
            {
                WaveformKey key;
                key.trackId = track.id;
                key.tileIndex = tileIdx;
                key.isStereo = drawStereo;
                key.startTimeSeconds = actualStart;  // Always in audio time (0-based)
                key.endTimeSeconds = actualEnd;       // Always in audio time
                
                keys.push_back(key);
            }
        }
    }
    
    return keys;
}

juce::Rectangle<int> VirtualTimelineComponent::getTileDestinationRect(const TrackRenderData& track, 
                                                                      double tileStartTime, 
                                                                      double tileEndTime) const
{
    // Convert tile's audio time back to component pixels
    const double componentStartTime = std::chrono::duration<double>(track.componentStartTime).count();
    
    // Get the actual cueStart (can be negative if adding silence before)
    double cueStartTime = 0.0;
    if (track.mixTrack)
    {
        cueStartTime = std::chrono::duration<double>(track.mixTrack->cueStart).count();
    }
    
    // When cueStart is negative, the audio starts later in the component
    // When cueStart is positive, the audio starts earlier (some is cut off)
    const double audioOffsetInComponent = std::max(0.0, -cueStartTime);
    
    // Convert from track audio time to component time
    // tileStartTime and tileEndTime are in audio time (0-based), not component time
    const double componentTileStart = tileStartTime + audioOffsetInComponent + componentStartTime;
    const double componentTileEnd = tileEndTime + audioOffsetInComponent + componentStartTime;
    
    // Convert to pixels based on current zoom
    const int startX = secondsToPixels(componentTileStart);
    const int endX = secondsToPixels(componentTileEnd);
    
    // Create destination rectangle within the track's waveform bounds
    // The tile will be stretched to fit the actual pixel size at this zoom level
    return juce::Rectangle<int>(
        startX,
        track.waveformBounds.getY(),
        endX - startX,
        track.waveformBounds.getHeight()
    );
}

void VirtualTimelineComponent::onTileRendered(const WaveformKey& key, juce::Image&& image)
{
    // This is called from the render thread.
    if (tileCache_)
    {
        tileCache_->putTile(key, std::move(image));
    }

    // Trigger an immediate repaint on the message thread to show the new tile ASAP
    juce::MessageManager::callAsync([this] { 
        repaint();  // Immediate repaint instead of scheduled frame
    });
}

} // namespace jucyaudio::ui