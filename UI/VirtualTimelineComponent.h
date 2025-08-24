#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <Audio/MixProjectLoader.h>
#include <Database/Includes/MixInfo.h>
#include <memory>
#include <vector>
#include <atomic>
#include <set>

namespace jucyaudio::ui {

//==============================================================================
/**
    Virtual timeline component that renders all tracks in a single paint() call
    for optimal performance with large track counts (100-1000+).
    
    This replaces the component-per-track architecture with a data-oriented
    virtual rendering approach.
*/
class VirtualTimelineComponent : public juce::Component,
                                 public juce::AsyncUpdater
{
public:
    using TrackId = int;
    
    struct TrackRenderData
    {
        TrackId id{};
        juce::String name;
        juce::Colour colour;
        
        // Time-based positioning (like original timeline)
        jucyaudio::Duration_t audioStartTime{0};      // Audio start time
        jucyaudio::Duration_t componentStartTime{0};  // Component start time (audio + cueStart)
        double effectiveDuration{0.0};                // Duration in seconds
        
        // Layout (computed once per resize/zoom)
        int laneIndex{0};                            // Which vertical lane (0, 1, 2, etc.)
        juce::Rectangle<int> bounds;                 // Complete component bounds
        juce::Rectangle<int> waveformBounds;         // Area for waveform rendering
        
        // Track data
        std::shared_ptr<database::MixTrack> mixTrack;
        const database::TrackInfo* trackInfo{nullptr};
        
        // Rendering state
        std::shared_ptr<juce::AudioThumbnail> thumbnail;
        bool selected{false};
        bool isVisible{false};  // Within viewport
    };
    
    //==============================================================================
    VirtualTimelineComponent(juce::AudioFormatManager& formatManager,
                           juce::AudioThumbnailCache& thumbnailCache);
    ~VirtualTimelineComponent() override;
    
    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    
    // AsyncUpdater override - frame scheduler
    void handleAsyncUpdate() override;
    
    // Public API
    void loadMixProject(audio::MixProjectLoader* loader);
    void setZoomLevel(double secondsPerPixel);
    void setPixelsPerSecond(double pixelsPerSecond);
    void setViewportBounds(const juce::Rectangle<int>& bounds);
    void setPlayheadPosition(double seconds);
    
    // Selection management
    void selectTrack(TrackId id, bool addToSelection = false);
    void clearSelection();
    std::vector<TrackId> getSelectedTracks() const;
    
    // Performance metrics
    struct PerfMetrics
    {
        std::atomic<int> paintsPerSecond{0};
        std::atomic<double> avgPaintTimeMs{0.0};
        std::atomic<int> visibleTracks{0};
        std::atomic<int> totalTracks{0};
    };
    
    const PerfMetrics& getMetrics() const { return metrics_; }
    
    // Frame scheduling
    void scheduleFrame();
    
    // Performance testing
    void runPerfHarness(int numTracks);
    
private:
    // Track management
    std::vector<TrackRenderData> tracks_;
    audio::MixProjectLoader* mixProjectLoader_{nullptr};
    juce::AudioFormatManager& formatManager_;
    juce::AudioThumbnailCache& thumbnailCache_;
    
    // View state
    double pixelsPerSecond_{100.0};  // Default zoom (inverse of secondsPerPixel)
    juce::Rectangle<int> viewportBounds_;
    double playheadSeconds_{0.0};
    
    // Layout constants (matching original timeline)
    static constexpr int rulerHeight{30};
    static constexpr int trackHeight{80};  // MixTrackComponent::TOTAL_COMPONENT_HEIGHT
    static constexpr int yGap{5};
    static constexpr int waveformInset{4};
    
    // Cached layout
    int cachedNumLanes_{-1};
    int calculatedWidth_{800};
    int calculatedHeight_{600};
    
    // Selection state
    std::set<TrackId> selectedTracks_;
    
    // Performance tracking
    mutable PerfMetrics metrics_;
    int paintCount_{0};
    juce::uint32 metricsResetTime_{0};
    
    // Helper methods
    void calculateTrackPositions();
    void refreshLayout();
    void updateVisibleTracks();
    TrackRenderData* getTrackAt(juce::Point<int> point);
    int secondsToPixels(double seconds) const;
    double pixelsToSeconds(int pixels) const;
    juce::Rectangle<int> getVisibleArea() const;
    
    // Rendering helpers
    void paintTracks(juce::Graphics& g);
    void paintTrack(juce::Graphics& g, const TrackRenderData& track);
    void paintGrid(juce::Graphics& g);
    void paintPlayhead(juce::Graphics& g);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VirtualTimelineComponent)
};

} // namespace jucyaudio::ui