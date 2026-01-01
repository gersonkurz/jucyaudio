#include "TrackRenderer.h"

namespace jucyaudio::ui {

//==============================================================================
void TrackRenderer::renderTrack(juce::Graphics& g, 
                               const RenderParams& params,
                               juce::AudioThumbnail* thumbnail)
{
    // Background and selection
    renderBackground(g, params.bounds, params.trackColour, params.isSelected);
    
    // Waveform
    if (thumbnail && thumbnail->getTotalLength() > 0)
    {
        const double endTime = params.startOffsetSeconds + params.durationSeconds;
        renderWaveform(g, params.waveformBounds, thumbnail, 
                      params.trackColour, params.startOffsetSeconds, endTime);
    }
    
    // Fade envelope
    if (params.fadeInSeconds > 0.0 || params.fadeOutSeconds > 0.0)
    {
        renderFadeEnvelope(g, params.waveformBounds,
                         params.fadeInSeconds, params.fadeOutSeconds,
                         params.durationSeconds, params.secondsPerPixel);
    }
}

//==============================================================================
void TrackRenderer::renderBackground(juce::Graphics& g, 
                                    const juce::Rectangle<int>& bounds,
                                    juce::Colour colour,
                                    bool isSelected)
{
    // Track background
    if (isSelected)
    {
        g.setColour(colour.withAlpha(0.2f));
        g.fillRect(bounds);
        
        // Selection border
        g.setColour(colour.brighter());
        g.drawRect(bounds.toFloat(), 2.0f);
    }
    else
    {
        g.setColour(colour.withAlpha(0.1f));
        g.fillRect(bounds);
        
        // Normal border
        g.setColour(colour.withAlpha(0.3f));
        g.drawRect(bounds.toFloat(), 0.5f);
    }
}

//==============================================================================
void TrackRenderer::renderWaveform(juce::Graphics& g,
                                  const juce::Rectangle<int>& bounds,
                                  juce::AudioThumbnail* thumbnail,
                                  juce::Colour colour,
                                  double startTime,
                                  double endTime)
{
    if (!thumbnail || thumbnail->getTotalLength() <= 0)
        return;
    
    const double thumbLength = thumbnail->getTotalLength();
    
    // Clamp times to valid range
    startTime = std::max(0.0, std::min(startTime, thumbLength));
    endTime = std::max(startTime, std::min(endTime, thumbLength));
    
    if (endTime <= startTime)
        return;
    
    // Draw waveform channels
    const int numChannels = thumbnail->getNumChannels();
    const int channelHeight = bounds.getHeight() / std::max(1, numChannels);
    
    g.setColour(colour.withAlpha(0.8f));
    
    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto channelBounds = bounds.withHeight(channelHeight)
                                         .withY(bounds.getY() + channel * channelHeight);
        
        thumbnail->drawChannel(g, channelBounds, startTime, endTime, channel, 1.0f);
    }
}

//==============================================================================
void TrackRenderer::renderFadeEnvelope(juce::Graphics& g,
                                      const juce::Rectangle<int>& bounds,
                                      double fadeInSeconds,
                                      double fadeOutSeconds,
                                      double totalSeconds,
                                      double secondsPerPixel)
{
    if (totalSeconds <= 0 || secondsPerPixel <= 0)
        return;
    
    const int totalWidth = bounds.getWidth();
    const float height = static_cast<float>(bounds.getHeight());
    const float y = static_cast<float>(bounds.getY());
    
    g.setColour(juce::Colours::yellow.withAlpha(0.6f));
    
    // Fade in
    if (fadeInSeconds > 0)
    {
        const int fadeInPixels = secondsToPixels(fadeInSeconds, secondsPerPixel);
        const int clampedFadeIn = std::min(fadeInPixels, totalWidth);
        
        juce::Path fadeIn;
        fadeIn.startNewSubPath(static_cast<float>(bounds.getX()), y + height);
        fadeIn.lineTo(static_cast<float>(bounds.getX() + clampedFadeIn), y);
        fadeIn.lineTo(static_cast<float>(bounds.getX() + clampedFadeIn), y + height);
        fadeIn.closeSubPath();
        
        g.fillPath(fadeIn);
        
        // Fade in line
        g.setColour(juce::Colours::yellow);
        g.drawLine(static_cast<float>(bounds.getX()), y + height,
                  static_cast<float>(bounds.getX() + clampedFadeIn), y, 2.0f);
    }
    
    // Fade out
    if (fadeOutSeconds > 0)
    {
        const int fadeOutPixels = secondsToPixels(fadeOutSeconds, secondsPerPixel);
        const int fadeOutStart = std::max(0, totalWidth - fadeOutPixels);
        
        juce::Path fadeOut;
        fadeOut.startNewSubPath(static_cast<float>(bounds.getX() + fadeOutStart), y);
        fadeOut.lineTo(static_cast<float>(bounds.getRight()), y + height);
        fadeOut.lineTo(static_cast<float>(bounds.getX() + fadeOutStart), y + height);
        fadeOut.closeSubPath();
        
        g.fillPath(fadeOut);
        
        // Fade out line
        g.setColour(juce::Colours::yellow);
        g.drawLine(static_cast<float>(bounds.getX() + fadeOutStart), y,
                  static_cast<float>(bounds.getRight()), y + height, 2.0f);
    }
}

//==============================================================================
void TrackRenderer::renderCuePoints(juce::Graphics& g,
                                   const juce::Rectangle<int>& bounds,
                                   const database::MixTrack& track,
                                   double secondsPerPixel)
{
    // Convert Duration_t (milliseconds) to seconds
    const double cueStartSeconds = track.cueStart.count() / 1000.0;
    const double cueEndSeconds = track.cueEnd.count() / 1000.0;
    
    // Render cue start marker
    if (cueStartSeconds > 0)
    {
        const int x = bounds.getX() + secondsToPixels(cueStartSeconds, secondsPerPixel);
        if (x >= bounds.getX() && x <= bounds.getRight())
        {
            g.setColour(juce::Colours::green);
            g.drawVerticalLine(x, static_cast<float>(bounds.getY()), 
                             static_cast<float>(bounds.getBottom()));
            
            // Draw marker
            juce::Path marker;
            marker.addTriangle(static_cast<float>(x - 4), static_cast<float>(bounds.getY()),
                             static_cast<float>(x + 4), static_cast<float>(bounds.getY()),
                             static_cast<float>(x), static_cast<float>(bounds.getY() + 8));
            g.fillPath(marker);
        }
    }
    
    // Render cue end marker
    if (cueEndSeconds > 0)
    {
        const int x = bounds.getX() + secondsToPixels(cueEndSeconds, secondsPerPixel);
        if (x >= bounds.getX() && x <= bounds.getRight())
        {
            g.setColour(juce::Colours::orange);
            g.drawVerticalLine(x, static_cast<float>(bounds.getY()), 
                             static_cast<float>(bounds.getBottom()));
            
            // Draw marker
            juce::Path marker;
            marker.addTriangle(static_cast<float>(x - 4), static_cast<float>(bounds.getBottom()),
                             static_cast<float>(x + 4), static_cast<float>(bounds.getBottom()),
                             static_cast<float>(x), static_cast<float>(bounds.getBottom() - 8));
            g.fillPath(marker);
        }
    }
}

//==============================================================================
void TrackRenderer::renderTitle(juce::Graphics& g,
                               const juce::Rectangle<int>& bounds,
                               const juce::String& title,
                               juce::Colour textColour)
{
    g.setColour(textColour);
    g.setFont(12.0f);
    g.drawText(title, bounds.reduced(4, 2), juce::Justification::topLeft, true);
}

//==============================================================================
void TrackRenderer::renderTimeInfo(juce::Graphics& g,
                                  const juce::Rectangle<int>& bounds,
                                  double durationSeconds,
                                  double bpm)
{
    juce::String info;
    
    if (durationSeconds > 0)
    {
        info = formatTime(durationSeconds);
    }
    
    if (bpm > 0)
    {
        if (info.isNotEmpty())
            info += " | ";
        info += juce::String(bpm, 1) + " BPM";
    }
    
    if (info.isNotEmpty())
    {
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.setFont(10.0f);
        g.drawText(info, bounds.reduced(4, 2), juce::Justification::topRight, true);
    }
}

//==============================================================================
juce::String TrackRenderer::formatTime(double seconds)
{
    const int mins = static_cast<int>(seconds) / 60;
    const int secs = static_cast<int>(seconds) % 60;
    const int millis = static_cast<int>((seconds - std::floor(seconds)) * 1000);
    
    return juce::String::formatted("%d:%02d.%03d", mins, secs, millis);
}

int TrackRenderer::secondsToPixels(double seconds, double secondsPerPixel)
{
    if (secondsPerPixel <= 0)
        return 0;
    return static_cast<int>(seconds / secondsPerPixel);
}

double TrackRenderer::pixelsToSeconds(int pixels, double secondsPerPixel)
{
    return pixels * secondsPerPixel;
}

//==============================================================================
void TrackRenderer::drawWaveformPath(juce::Graphics& g,
                                    const juce::Path& path,
                                    juce::Colour colour,
                                    const juce::Rectangle<int>& bounds)
{
    g.saveState();
    g.reduceClipRegion(bounds);
    g.setColour(colour);
    g.fillPath(path);
    g.restoreState();
}

} // namespace jucyaudio::ui