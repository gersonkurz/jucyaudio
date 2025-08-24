#include "VirtualTimelineComponent.h"
#include <Database/Includes/Constants.h>
#include <Database/TrackLibrary.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <UI/Settings.h>

namespace jucyaudio::ui {

//==============================================================================
VirtualTimelineComponent::VirtualTimelineComponent(juce::AudioFormatManager& formatManager,
                                                   juce::AudioThumbnailCache& thumbnailCache)
    : formatManager_(formatManager)
    , thumbnailCache_(thumbnailCache)
{
    setOpaque(true);  // Critical for performance - we paint the entire background
    metricsResetTime_ = juce::Time::getMillisecondCounter();
}

VirtualTimelineComponent::~VirtualTimelineComponent()
{
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
    
    // Calculate component size first
    double maxTimeSecs = 0.0;
    for (const auto& track : tracks_)
    {
        const double startTime = std::chrono::duration<double>(track.componentStartTime).count();
        const double endTime = startTime + track.effectiveDuration;
        maxTimeSecs = std::max(maxTimeSecs, endTime);
    }
    
    calculatedWidth_ = static_cast<int>(maxTimeSecs * pixelsPerSecond_) + 200; // Extra padding
    
    // Calculate height based on viewport or default
    if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
    {
        calculatedHeight_ = std::max(600, viewport->getHeight());
    }
    else
    {
        calculatedHeight_ = 600; // Default height
    }
    
    // CRITICAL: Only call setSize if size actually changed to avoid resize loops
    if (getWidth() != calculatedWidth_ || getHeight() != calculatedHeight_)
    {
        setSize(calculatedWidth_, calculatedHeight_);
        return; // resized() will call refreshLayout()
    }
    
    // Size didn't change, just refresh layout
    refreshLayout();
}

void VirtualTimelineComponent::refreshLayout()
{
    // Calculate available height for lanes
    const int availableHeightForLanes = getHeight() - rulerHeight;
    const int numLanes = std::max(1, availableHeightForLanes / (trackHeight + yGap));
    
    // Only recalculate if lanes changed (optimization from original)
    if (numLanes == cachedNumLanes_)
    {
        updateVisibleTracks();
        return; // CRITICAL: Skip expensive recalculation
    }
    
#if JUCE_DEBUG
    spdlog::debug("VirtualTimeline: Lanes changed {} -> {}, recalculating positions", cachedNumLanes_, numLanes);
#endif
    cachedNumLanes_ = numLanes;
    
    // Assign tracks to lanes (simple round-robin for now)
    int currentLane = 0;
    int laneDirection = +1;
    
    for (auto& track : tracks_)
    {
        // Calculate X position based on component start time
        const double startTime = std::chrono::duration<double>(track.componentStartTime).count();
        const int startX = static_cast<int>(startTime * pixelsPerSecond_);
        
        // Calculate width based on duration
        const int width = static_cast<int>(track.effectiveDuration * pixelsPerSecond_);
        
        // Calculate Y position based on lane
        const int yPos = rulerHeight + (currentLane * (trackHeight + yGap));
        
        // Set bounds
        track.laneIndex = currentLane;
        track.bounds = juce::Rectangle<int>(startX, yPos, width, trackHeight);
        track.waveformBounds = track.bounds.reduced(waveformInset);
        
        // Advance to next lane (with alternating direction like original)
        if ((currentLane + laneDirection) >= numLanes || (currentLane + laneDirection) < 0)
            laneDirection *= -1;
        currentLane += laneDirection;
        if (numLanes == 1)
            currentLane = 0;
    }
    
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
    
    // --- PHASE 3: Render waveform ---    
    if (track.thumbnail && track.thumbnail->getNumChannels() > 0)
    {
        g.setColour(track.colour);
        const double thumbnailStartTime = std::chrono::duration<double>(
            std::max(jucyaudio::Duration_t{0}, track.mixTrack->cueStart)).count();
        const double thumbnailEndTime = std::chrono::duration<double>(track.trackInfo->duration).count();

        const bool drawStereo = config::theSettings.mixEditingSettings.drawStereoWaveforms;

        if (drawStereo && track.thumbnail->getNumChannels() > 1)
        {
            // Draw stereo channels separately
            auto topHalf = track.waveformBounds.withHeight(track.waveformBounds.getHeight() / 2);
            auto bottomHalf = track.waveformBounds.withY(topHalf.getBottom());
            track.thumbnail->drawChannel(g, topHalf, thumbnailStartTime, thumbnailEndTime, 0, 1.0f);
            track.thumbnail->drawChannel(g, bottomHalf, thumbnailStartTime, thumbnailEndTime, 1, 1.0f);
        }
        else
        {
            // Draw combined or mono waveform (channel 0)
            track.thumbnail->drawChannel(g, track.waveformBounds, thumbnailStartTime, thumbnailEndTime, 0, 1.0f);
        }
    }
    else
    {
        // Fallback: placeholder waveform
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

} // namespace jucyaudio::ui