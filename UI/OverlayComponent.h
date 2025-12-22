#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace jucyaudio::ui {

//==============================================================================
/**
    Lightweight overlay component for rendering transient graphics like
    playhead, drag previews, and selection marquees.

    This component sits on top of the TimelineComponent and only
    repaints small regions for optimal performance.
*/
class OverlayComponent : public juce::Component
{
public:
    OverlayComponent();
    ~OverlayComponent() override;
    
    // Component overrides
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    
    // Playhead management
    void setPlayheadPosition(double seconds);
    void setSecondsPerPixel(double secondsPerPixel);
    
    // Selection marquee
    void startMarqueeSelection(juce::Point<int> startPoint);
    void updateMarqueeSelection(juce::Point<int> currentPoint);
    void endMarqueeSelection();
    
    // Drag preview
    void startDragPreview(juce::Rectangle<int> originalBounds);
    void updateDragPreview(juce::Point<int> offset);
    void endDragPreview();
    
    // Stripe repaint optimization
    void repaintStripe(int x, int width = 7);
    
private:
    // Playhead state
    double playheadSeconds_{0.0};
    double secondsPerPixel_{0.01};
    int previousPlayheadX_{-1};
    
    // Selection marquee state
    bool isDrawingMarquee_{false};
    juce::Rectangle<int> marqueeRect_;
    juce::Point<int> marqueeStart_;
    
    // Drag preview state
    bool isShowingDragPreview_{false};
    juce::Rectangle<int> dragPreviewBounds_;
    juce::Rectangle<int> originalDragBounds_;
    
    // Helper methods
    int secondsToPixels(double seconds) const;
    void paintPlayhead(juce::Graphics& g);
    void paintMarquee(juce::Graphics& g);
    void paintDragPreview(juce::Graphics& g);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OverlayComponent)
};

} // namespace jucyaudio::ui