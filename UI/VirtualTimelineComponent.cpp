#include "VirtualTimelineComponent.h"
#include <Database/Includes/Constants.h>
#include <Database/TrackLibrary.h>
#include <UI/Settings.h>
#include <spdlog/spdlog.h>
#include <chrono>

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
        return nullptr;
    }

    void putTile(const WaveformKey& key, juce::Image&& image)
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (cache_.find(key) != cache_.end())
            return; // Already exists

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
        juce::Image tileImage(juce::Image::ARGB, request.tileBoundsInComponent.getWidth(), request.tileBoundsInComponent.getHeight(), true);
        if (!request.thumbnail)
            return tileImage;

        juce::Graphics g(tileImage);
        
        // Determine LOD based on zoom bucket
        // Higher zoom bucket = more zoomed out = less detail needed
        const int lodLevel = std::max(0, std::min(4, request.key.zoomBucket));
        
        // Use the track's color
        g.setColour(request.trackColour);
        
        // Check if we need stereo rendering
        const bool drawStereo = config::theSettings.mixEditingSettings.drawStereoWaveforms.get();
        const int numChannels = request.thumbnail->getNumChannels();
        
        spdlog::debug("[TILE_RENDER] Track {} tile {}: drawStereo={}, numChannels={}, zoom={}, keyStereo={}", 
                     request.key.trackId, request.key.tileIndex, drawStereo, numChannels, request.key.zoomBucket, request.key.isStereo);
        
        // Calculate vertical zoom based on LOD level
        // At higher LOD (zoomed out), we reduce detail to improve rendering speed
        float verticalZoom = 1.0f;
        switch(lodLevel)
        {
            case 0: verticalZoom = 1.0f; break;    // Maximum detail (very zoomed in)
            case 1: verticalZoom = 0.9f; break;    // High detail
            case 2: verticalZoom = 0.7f; break;    // Medium detail
            case 3: verticalZoom = 0.5f; break;    // Low detail
            case 4: verticalZoom = 0.3f; break;    // Minimum detail (very zoomed out)
            default: verticalZoom = 0.3f; break;
        }
        
        if (drawStereo && numChannels > 1)
        {
            // Split the tile vertically for stereo
            auto topHalf = tileImage.getBounds().withHeight(tileImage.getHeight() / 2);
            auto bottomHalf = tileImage.getBounds().withY(topHalf.getBottom()).withHeight(tileImage.getHeight() - topHalf.getHeight());
            
            request.thumbnail->drawChannel(g, topHalf, request.startTimeSecs, request.endTimeSecs, 0, verticalZoom);
            request.thumbnail->drawChannel(g, bottomHalf, request.startTimeSecs, request.endTimeSecs, 1, verticalZoom);
        }
        else
        {
            // Single waveform view - draw channel 0 only (which shows mono or left channel)
            // This gives us a single waveform regardless of channel count
            request.thumbnail->drawChannel(g, tileImage.getBounds(), request.startTimeSecs, request.endTimeSecs, 0, verticalZoom);
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
    metricsResetTime_ = juce::Time::getMillisecondCounter();

    tileCache_ = std::make_unique<WaveformTileCache>(256); // 256MB cache
    tileRenderer_ = std::make_unique<TileRenderQueue>();
    tileRenderer_->onTileReady = [this](const WaveformKey& key, juce::Image&& image) {
        onTileRendered(key, std::move(image));
    };
    tileRenderer_->start();
}

VirtualTimelineComponent::~VirtualTimelineComponent()
{
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
    refreshLayout();
    queueMissingTiles();
    scheduleFrame();
}

//==============================================================================
void VirtualTimelineComponent::mouseDown(const juce::MouseEvent& event)
{
    if (auto* track = getTrackAt(event.position.toInt()))
    {
        const bool addToSelection = event.mods.isShiftDown() || event.mods.isCommandDown();
        selectTrack(track->id, addToSelection);
        scheduleFrame();
    }
    else if (!event.mods.isShiftDown())
    {
        clearSelection();
        scheduleFrame();
    }
}

void VirtualTimelineComponent::mouseDrag(const juce::MouseEvent& event)
{
    // TODO: Implement drag selection, cue point dragging, etc.
}

void VirtualTimelineComponent::mouseUp(const juce::MouseEvent& event)
{
    // TODO: Complete drag operations
}

void VirtualTimelineComponent::mouseMove(const juce::MouseEvent& event)
{
    // TODO: Update hover states, cursors
}

void VirtualTimelineComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    // Zoom constants (matching original TimelineComponent)
    constexpr double ZOOM_FACTOR = 1.1;
    constexpr double MIN_ZOOM = 1.0;    // 1 pixel per second (zoomed out)
    constexpr double MAX_ZOOM = 2000.0; // 2000 pixels per second (zoomed in)
    
    // Get mouse position relative to timeline
    const auto mousePos = event.getPosition();
    
    // Calculate time position at mouse cursor
    const double timeAtMouse = mousePos.x / pixelsPerSecond_;
    
    // Calculate new zoom level
    const double zoomDelta = wheel.deltaY > 0 ? ZOOM_FACTOR : (1.0 / ZOOM_FACTOR);
    const double newZoom = juce::jlimit(MIN_ZOOM, MAX_ZOOM, pixelsPerSecond_ * zoomDelta);
    
    if (newZoom != pixelsPerSecond_)
    {
        pixelsPerSecond_ = newZoom;
        calculateTrackPositions();
        queueMissingTiles();
        
        // Notify the parent component about zoom change if needed
        // This would typically trigger the onZoomChanged callback
        scheduleFrame();
    }
}

//==============================================================================
void VirtualTimelineComponent::handleAsyncUpdate()
{
    // Frame scheduler - coalesces multiple repaint requests
    repaint();
}

void VirtualTimelineComponent::scheduleFrame()
{
    // Coalesce repaint requests via AsyncUpdater
    triggerAsyncUpdate();
}

//==============================================================================
void VirtualTimelineComponent::loadMixProject(audio::MixProjectLoader* loader)
{
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
        data.id = mixTrack.trackId;
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
        
        // Generate color
        data.colour = juce::Colour::fromHSV(
            static_cast<float>(i) / static_cast<float>(mixTracks.size()),
            0.6f, 0.8f, 1.0f);
        
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
                }
            }
        }
        
        tracks_.push_back(std::move(data));
    }
    
    metrics_.totalTracks = static_cast<int>(tracks_.size());
    calculateTrackPositions();
    updateVisibleTracks();
    queueMissingTiles();  // Start generating tiles immediately after loading
    scheduleFrame();
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
        viewportBounds_ = bounds;
        updateVisibleTracks();
        queueMissingTiles();
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
}

void VirtualTimelineComponent::clearSelection()
{
    selectedTracks_.clear();
    for (auto& track : tracks_)
    {
        track.selected = false;
    }
}

std::vector<VirtualTimelineComponent::TrackId> VirtualTimelineComponent::getSelectedTracks() const
{
    return std::vector<TrackId>(selectedTracks_.begin(), selectedTracks_.end());
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
    const int availableHeightForLanes = (viewportBounds_.getHeight() > 0 ? viewportBounds_.getHeight() : 600) - rulerHeight;
    const int numLanes = std::max(1, availableHeightForLanes / (trackHeight + yGap));
    
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
    
    for (auto& track : tracks_)
    {
        track.isVisible = track.bounds.intersects(visibleArea);
        if (track.isVisible)
            visibleCount++;
    }
    
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
    // Track background
    if (track.selected)
    {
        g.setColour(getLookAndFeel().findColour(juce::TextEditor::highlightColourId).withAlpha(0.3f));
        g.fillRect(track.bounds);
    }
    
    // Track border
    g.setColour(getLookAndFeel().findColour(juce::TextEditor::outlineColourId));
    g.drawRect(track.bounds.toFloat(), 0.5f);
    
    // --- Simplified: Direct rendering only (tiling disabled for now) ---
    if (track.thumbnail && track.thumbnail->isFullyLoaded())
    {
        g.setColour(track.colour);
        const double thumbnailStartTime = std::chrono::duration<double>(
            std::max(jucyaudio::Duration_t{0}, track.mixTrack->cueStart)).count();
        const double thumbnailEndTime = std::chrono::duration<double>(track.trackInfo->duration).count();

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
            // Single waveform - just draw channel 0 (mono or left channel)
            track.thumbnail->drawChannel(g, track.waveformBounds, thumbnailStartTime, thumbnailEndTime, 0, 1.0f);
        }
    }
    else
    {
        // Fallback: placeholder for tracks without a thumbnail or if it's still loading
        g.setColour(track.colour.withAlpha(0.5f));
        g.fillRect(track.waveformBounds);
        g.setColour(juce::Colours::black.withAlpha(0.2f));
        g.drawRect(track.waveformBounds);
    }
    
    // Track name
    g.setColour(getLookAndFeel().findColour(juce::Label::textColourId));
    g.setFont(12.0f);
    g.drawText(track.name, track.bounds.reduced(4, 2), 
               juce::Justification::topLeft, true);
}

void VirtualTimelineComponent::paintGrid(juce::Graphics& g)
{
    // Draw time grid lines
    g.setColour(getLookAndFeel().findColour(juce::TextEditor::outlineColourId).withAlpha(0.2f));
    
    const double secondsPerGridLine = 1.0; // 1 second grid for now
    const int pixelsPerGridLine = secondsToPixels(secondsPerGridLine);
    
    if (pixelsPerGridLine > 5) // Only draw if grid lines are spaced enough
    {
        for (int x = 0; x < getWidth(); x += pixelsPerGridLine)
        {
            g.drawVerticalLine(x, 0.0f, static_cast<float>(getHeight()));
        }
    }
}

void VirtualTimelineComponent::paintPlayhead(juce::Graphics& g)
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
        track.colour = juce::Colour::fromHSV(
            static_cast<float>(i) / static_cast<float>(numTracks),
            0.6f, 0.8f, 1.0f);
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

int VirtualTimelineComponent::getZoomBucket() const
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

void VirtualTimelineComponent::queueMissingTiles()
{
    if (!mixProjectLoader_)
        return;

    const auto visibleBounds = getVisibleArea();
    
    // Add prefetch margin (1 tile on each side for smooth scrolling)
    const int prefetchMargin = tileWidth_;
    auto prefetchBounds = visibleBounds.expanded(prefetchMargin, 0);

    for (const auto& track : tracks_)
    {
        // Check if track is visible or within prefetch area
        const bool inPrefetchArea = track.bounds.intersects(prefetchBounds);
        if (!inPrefetchArea || !track.thumbnail || !track.trackInfo)
            continue;

        const int currentZoomBucket = getZoomBucket();
        
        // Calculate which tiles overlap with this track's bounds
        const int firstTileX = (track.bounds.getX() / tileWidth_) * tileWidth_;
        const int lastTileX = ((track.bounds.getRight() + tileWidth_ - 1) / tileWidth_) * tileWidth_;
        
        // Queue tiles for prefetch area (not just visible area)
        const int prefetchFirstTileX = std::max(firstTileX, (prefetchBounds.getX() / tileWidth_) * tileWidth_);
        const int prefetchLastTileX = std::min(lastTileX, ((prefetchBounds.getRight() + tileWidth_ - 1) / tileWidth_) * tileWidth_);

        // Get the current stereo setting for the key
        const bool drawStereo = config::theSettings.mixEditingSettings.drawStereoWaveforms.get();
        
        spdlog::info("[QUEUE_TILES] Track {} queueing tiles: drawStereo={}", track.id, drawStereo);
        
        for (int tileX = prefetchFirstTileX; tileX < prefetchLastTileX; tileX += tileWidth_)
        {
            const int tileIndex = tileX / tileWidth_;
            WaveformKey key{track.id, currentZoomBucket, tileIndex, drawStereo};
            
            if (!tileCache_->getTile(key))
            {
                // Calculate the time range for this tile within the track's audio
                // We need to map from component space to track's audio time
                const double componentStartSecs = std::chrono::duration<double>(track.componentStartTime).count();
                const double cueStartSecs = std::chrono::duration<double>(
                    std::max(jucyaudio::Duration_t{0}, track.mixTrack->cueStart)).count();
                const double trackDurationSecs = std::chrono::duration<double>(track.trackInfo->duration).count();
                
                // Map tile X position to time in the track's audio
                const double tileStartInComponentTime = pixelsToSeconds(tileX);
                const double tileEndInComponentTime = pixelsToSeconds(tileX + tileWidth_);
                
                // Convert to track's audio time (accounting for cue start)
                const double tileStartInTrackTime = (tileStartInComponentTime - componentStartSecs) + cueStartSecs;
                const double tileEndInTrackTime = (tileEndInComponentTime - componentStartSecs) + cueStartSecs;
                
                // Clamp to track's audio bounds
                const double clampedStartTime = std::max(0.0, std::min(trackDurationSecs, tileStartInTrackTime));
                const double clampedEndTime = std::max(0.0, std::min(trackDurationSecs, tileEndInTrackTime));

                juce::Rectangle<int> tileBounds(0, 0, tileWidth_, track.bounds.getHeight());

                tileRenderer_->requestTile({key,
                                          track.thumbnail,
                                          tileBounds,
                                          clampedStartTime,
                                          clampedEndTime,
                                          track.colour,
                                          cueStartSecs,
                                          trackDurationSecs});
            }
        }
    }
}

void VirtualTimelineComponent::onTileRendered(const WaveformKey& key, juce::Image&& image)
{
    // This is called from the render thread.
    if (tileCache_)
    {
        tileCache_->putTile(key, std::move(image));
    }

    // We need to trigger a repaint on the message thread.
    juce::MessageManager::callAsync([this] { 
        scheduleFrame(); 
    });
}

} // namespace jucyaudio::ui