#include "OverlayComponent.h"

namespace jucyaudio::ui {

//==============================================================================
OverlayComponent::OverlayComponent()
{
    // Transparent overlay - doesn't paint background
    setOpaque(false);
    setInterceptsMouseClicks(false, false); // Pass through mouse events
}

OverlayComponent::~OverlayComponent() = default;

//==============================================================================
void OverlayComponent::paint(juce::Graphics& g)
{
    // Only paint overlay elements
    paintPlayhead(g);
    paintMarquee(g);
    paintDragPreview(g);
}

void OverlayComponent::mouseDown(const juce::MouseEvent& event)
{
    // Start marquee selection on middle or right button
    if (event.mods.isMiddleButtonDown() || 
        (event.mods.isLeftButtonDown() && event.mods.isAltDown()))
    {
        startMarqueeSelection(event.position.toInt());
    }
}

void OverlayComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (isDrawingMarquee_)
    {
        updateMarqueeSelection(event.position.toInt());
    }
}

void OverlayComponent::mouseUp(const juce::MouseEvent& event)
{
    if (isDrawingMarquee_)
    {
        endMarqueeSelection();
    }
}

//==============================================================================
void OverlayComponent::setPlayheadPosition(double seconds)
{
    if (seconds != playheadSeconds_)
    {
        // Calculate old and new positions
        const int oldX = secondsToPixels(playheadSeconds_);
        const int newX = secondsToPixels(seconds);
        
        playheadSeconds_ = seconds;
        
        // Stripe repaint - only repaint affected areas
        if (previousPlayheadX_ >= 0)
        {
            repaintStripe(previousPlayheadX_);
        }
        repaintStripe(newX);
        
        previousPlayheadX_ = newX;
    }
}

void OverlayComponent::setSecondsPerPixel(double secondsPerPixel)
{
    if (secondsPerPixel != secondsPerPixel_ && secondsPerPixel > 0.0)
    {
        // Clear old playhead position
        if (previousPlayheadX_ >= 0)
        {
            repaintStripe(previousPlayheadX_);
        }
        
        secondsPerPixel_ = secondsPerPixel;
        
        // Repaint new playhead position
        const int newX = secondsToPixels(playheadSeconds_);
        repaintStripe(newX);
        previousPlayheadX_ = newX;
    }
}

//==============================================================================
void OverlayComponent::startMarqueeSelection(juce::Point<int> startPoint)
{
    isDrawingMarquee_ = true;
    marqueeStart_ = startPoint;
    marqueeRect_ = juce::Rectangle<int>(startPoint.x, startPoint.y, 0, 0);
}

void OverlayComponent::updateMarqueeSelection(juce::Point<int> currentPoint)
{
    if (!isDrawingMarquee_)
        return;
    
    // Repaint old marquee area
    repaint(marqueeRect_.expanded(2));
    
    // Calculate new marquee rectangle
    const int x = std::min(marqueeStart_.x, currentPoint.x);
    const int y = std::min(marqueeStart_.y, currentPoint.y);
    const int w = std::abs(currentPoint.x - marqueeStart_.x);
    const int h = std::abs(currentPoint.y - marqueeStart_.y);
    
    marqueeRect_ = juce::Rectangle<int>(x, y, w, h);
    
    // Repaint new marquee area
    repaint(marqueeRect_.expanded(2));
}

void OverlayComponent::endMarqueeSelection()
{
    if (isDrawingMarquee_)
    {
        // Clear marquee
        repaint(marqueeRect_.expanded(2));
        isDrawingMarquee_ = false;
        marqueeRect_ = {};
    }
}

//==============================================================================
void OverlayComponent::startDragPreview(juce::Rectangle<int> originalBounds)
{
    isShowingDragPreview_ = true;
    originalDragBounds_ = originalBounds;
    dragPreviewBounds_ = originalBounds;
}

void OverlayComponent::updateDragPreview(juce::Point<int> offset)
{
    if (!isShowingDragPreview_)
        return;
    
    // Repaint old preview area
    repaint(dragPreviewBounds_.expanded(2));
    
    // Update preview position
    dragPreviewBounds_ = originalDragBounds_.translated(offset.x, offset.y);
    
    // Repaint new preview area
    repaint(dragPreviewBounds_.expanded(2));
}

void OverlayComponent::endDragPreview()
{
    if (isShowingDragPreview_)
    {
        // Clear preview
        repaint(dragPreviewBounds_.expanded(2));
        isShowingDragPreview_ = false;
        dragPreviewBounds_ = {};
    }
}

//==============================================================================
void OverlayComponent::repaintStripe(int x, int width)
{
    // Optimized stripe repaint for vertical elements like playhead
    repaint(x - width/2, 0, width, getHeight());
}

//==============================================================================
int OverlayComponent::secondsToPixels(double seconds) const
{
    return static_cast<int>(seconds / secondsPerPixel_);
}

void OverlayComponent::paintPlayhead(juce::Graphics& g)
{
    const int playheadX = secondsToPixels(playheadSeconds_);
    
    if (playheadX >= 0 && playheadX < getWidth())
    {
        // Playhead line
        g.setColour(juce::Colours::red);
        g.drawVerticalLine(playheadX, 0.0f, static_cast<float>(getHeight()));
        
        // Playhead triangle at top
        juce::Path triangle;
        triangle.addTriangle(static_cast<float>(playheadX - 5), 0.0f,
                           static_cast<float>(playheadX + 5), 0.0f,
                           static_cast<float>(playheadX), 10.0f);
        g.fillPath(triangle);
        
        // Playhead triangle at bottom
        triangle.clear();
        triangle.addTriangle(static_cast<float>(playheadX - 5), static_cast<float>(getHeight()),
                           static_cast<float>(playheadX + 5), static_cast<float>(getHeight()),
                           static_cast<float>(playheadX), static_cast<float>(getHeight() - 10));
        g.fillPath(triangle);
    }
}

void OverlayComponent::paintMarquee(juce::Graphics& g)
{
    if (isDrawingMarquee_ && !marqueeRect_.isEmpty())
    {
        // Draw selection marquee
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.fillRect(marqueeRect_);
        
        g.setColour(juce::Colours::white);
        g.drawRect(marqueeRect_.toFloat(), 1.0f);
    }
}

void OverlayComponent::paintDragPreview(juce::Graphics& g)
{
    if (isShowingDragPreview_ && !dragPreviewBounds_.isEmpty())
    {
        // Draw semi-transparent preview of dragged item
        g.setColour(juce::Colours::lightblue.withAlpha(0.5f));
        g.fillRect(dragPreviewBounds_);
        
        g.setColour(juce::Colours::lightblue);
        g.drawRect(dragPreviewBounds_.toFloat(), 2.0f);
    }
}

} // namespace jucyaudio::ui