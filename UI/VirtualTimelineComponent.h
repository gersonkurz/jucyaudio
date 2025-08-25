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
                                 public juce::AsyncUpdater,
                                 public juce::ChangeListener
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
    // Tiling System Data Structures
    //==============================================================================
    struct WaveformKey
    {
        TrackId trackId;
        int tileIndex;           // Global tile index (based on fixed grid)
        bool isStereo;           // Whether this is a stereo or mono render
        
        // Store the actual time range for rendering
        double startTimeSeconds;  // Start time in track's audio space
        double endTimeSeconds;    // End time in track's audio space
        
        bool operator==(const WaveformKey& other) const
        {
            // Only compare the key fields, not the time range
            // Removed zoomLevel to reuse tiles across zoom levels
            return trackId == other.trackId && 
                   tileIndex == other.tileIndex &&
                   isStereo == other.isStereo;
        }
    };
    
    struct WaveformKeyHash
    {
        std::size_t operator()(const WaveformKey& key) const
        {
            return std::hash<int>{}(key.trackId) ^ 
                   (std::hash<int>{}(key.tileIndex) << 1) ^ 
                   (std::hash<bool>{}(key.isStereo) << 2);
        }
    };
    
    struct WaveformTile
    {
        juce::Image image;
        std::atomic<bool> isReady{false};
    };
    
    //==============================================================================
    // Tiling System Classes
    //==============================================================================
    class WaveformTileCache;
    class TileRenderQueue;
    
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
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    
    // AsyncUpdater override - frame scheduler
    void handleAsyncUpdate() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    
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
    
    // Callbacks for changes (similar to old TimelineComponent)
    std::function<void(int orderInMix, const database::MixTrack&)> onCueAttachChanged;
    std::function<void(int orderInMix, const std::vector<database::EnvelopePoint>&)> onEnvelopeChanged;
    std::function<void(int orderInMix, jucyaudio::Duration_t cueStart, jucyaudio::Duration_t cueEnd)> onCuePointsChanged;
    std::function<void(double)> onMixPlaybackRequested;  // For keyboard shortcuts (toggles)
    std::function<void(double)> onMixPlaybackAlwaysRequested;  // For double-clicks (always plays)
    std::function<void(double)> onSeekRequested;
    std::function<void()> onDeleteTracksRequested;  // For delete key
    std::function<void(const std::vector<database::MixTrack>&, int position, bool before)> onPasteTracksRequested;
    std::function<void(int afterOrder)> onRemoveFollowingTracksRequested;
    
    // Keyboard handling
    bool keyPressed(const juce::KeyPress& key) override;
    
private:
    // Track management
    std::vector<TrackRenderData> tracks_;
    audio::MixProjectLoader* mixProjectLoader_{nullptr};
    juce::AudioFormatManager& formatManager_;
    juce::AudioThumbnailCache& thumbnailCache_;
    
    // View state
    double pixelsPerSecond_{2.0};  // Default zoom - very zoomed out to see entire mix
    juce::Rectangle<int> viewportBounds_;
    double playheadSeconds_{-1.0};  // Playback position (-1 means not playing)
    double clickedTimePosition_{-1.0};  // Last clicked position (-1 means never clicked)
    
    // Layout constants (matching original timeline)
    static constexpr int rulerHeight{30};
    static constexpr int trackHeight{80};  // MixTrackComponent::TOTAL_COMPONENT_HEIGHT
    static constexpr int yGap{5};
    static constexpr int waveformInset{4};
    
    // Cached layout
    int calculatedWidth_{800};
    int calculatedHeight_{600};
    
    // Selection state
    std::set<TrackId> selectedTracks_;
    
    // Clipboard state for copy/paste
    std::vector<database::MixTrack> clipboardTracks_;
    
    // Drag state for attach points and cue points
    struct DragState {
        bool isDragging{false};
        enum class DragType {
            None,
            AttachFrom,
            AttachTo,
            EnvelopePoint,
            CueStart,
            CueEnd
        } dragType{DragType::None};
        TrackId trackId{0};
        juce::Point<int> dragStartPoint;
        int draggedPointIndex{-1};  // For envelope points
        double originalTime{0.0};    // Original time value being dragged
        double currentTime{0.0};     // Current drag position in time
    };
    DragState dragState_;
    
    // Performance tracking
    mutable PerfMetrics metrics_;
    int paintCount_{0};
    juce::uint32 metricsResetTime_{0};

    // Waveform tiling system (Phase 3)
    std::unique_ptr<WaveformTileCache> tileCache_;
    std::unique_ptr<TileRenderQueue> tileRenderer_;
    static constexpr int tileWidth_{256};  // Pixels per tile
    static constexpr double sqrt2_{1.4142135623730951};
    
    // Helper methods
    void calculateTrackPositions();
    void refreshLayout();
    void updateVisibleTracks();
    TrackRenderData* getTrackAt(juce::Point<int> point);
    int secondsToPixels(double seconds) const;
    double pixelsToSeconds(int pixels) const;
    juce::Rectangle<int> getVisibleArea() const;
    
    // Hit testing for interactive elements
    enum class HitTestResult {
        None,
        Track,
        AttachFrom,
        AttachTo,
        EnvelopePoint,
        CueStart,
        CueEnd
    };
    struct HitTestInfo {
        HitTestResult type{HitTestResult::None};
        TrackRenderData* track{nullptr};
        int pointIndex{-1};  // For envelope points
    };
    HitTestInfo hitTest(juce::Point<int> point) const;
    
    // Tiling system helpers
    int getZoomLevel() const;
    void queueVisibleTiles();
    void queueTilesForTrack(const TrackRenderData& track, const juce::Rectangle<int>& visibleArea);
    void onTileRendered(const WaveformKey& key, juce::Image&& image);
    std::vector<WaveformKey> getTileKeysForTrack(const TrackRenderData& track, const juce::Rectangle<int>& visibleArea) const;
    juce::Rectangle<int> getTileDestinationRect(const TrackRenderData& track, double tileStartTime, double tileEndTime) const;

    // Rendering helpers
    void paintTracks(juce::Graphics& g);
    void paintTrack(juce::Graphics& g, const TrackRenderData& track);
    void paintGrid(juce::Graphics& g);
    void paintPlayhead(juce::Graphics& g);
    
    // Context menu helpers
    void showContextMenu(juce::Point<int> position);
    void handleContextMenuResult(int result);
    void copySelectedTracks();
    void deleteSelectedTracks();
    void pasteTracksBeforeSelection();
    void pasteTracksAfterSelection();
    void removeAllFollowingTracks();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VirtualTimelineComponent)
};

} // namespace jucyaudio::ui