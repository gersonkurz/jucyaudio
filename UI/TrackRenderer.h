#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <Database/Includes/MixInfo.h>

namespace jucyaudio::ui {

//==============================================================================
/**
    Static helper class for rendering track elements.
    
    All methods are stateless and allocation-free for use in hot paint paths.
    This extracts the rendering logic from MixTrackComponent for reuse in
    the virtual timeline.
*/
class TrackRenderer
{
public:
    struct RenderParams
    {
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> waveformBounds;
        juce::Colour trackColour;
        bool isSelected{false};
        bool isHovered{false};
        bool isMuted{false};
        double startOffsetSeconds{0.0};
        double durationSeconds{0.0};
        double fadeInSeconds{0.0};
        double fadeOutSeconds{0.0};
        double secondsPerPixel{0.01};
    };
    
    // Main rendering entry point
    static void renderTrack(juce::Graphics& g, 
                           const RenderParams& params,
                           juce::AudioThumbnail* thumbnail = nullptr);
    
    // Individual element renderers
    static void renderBackground(juce::Graphics& g, 
                                const juce::Rectangle<int>& bounds,
                                juce::Colour colour,
                                bool isSelected);
    
    static void renderWaveform(juce::Graphics& g,
                              const juce::Rectangle<int>& bounds,
                              juce::AudioThumbnail* thumbnail,
                              juce::Colour colour,
                              double startTime,
                              double endTime);
    
    static void renderFadeEnvelope(juce::Graphics& g,
                                  const juce::Rectangle<int>& bounds,
                                  double fadeInSeconds,
                                  double fadeOutSeconds,
                                  double totalSeconds,
                                  double secondsPerPixel);
    
    static void renderCuePoints(juce::Graphics& g,
                               const juce::Rectangle<int>& bounds,
                               const database::MixTrack& track,
                               double secondsPerPixel);
    
    static void renderTitle(juce::Graphics& g,
                          const juce::Rectangle<int>& bounds,
                          const juce::String& title,
                          juce::Colour textColour);
    
    static void renderTimeInfo(juce::Graphics& g,
                             const juce::Rectangle<int>& bounds,
                             double durationSeconds,
                             double bpm);
    
    // Utility methods
    static juce::String formatTime(double seconds);
    static int secondsToPixels(double seconds, double secondsPerPixel);
    static double pixelsToSeconds(int pixels, double secondsPerPixel);
    
private:
    // Private constructor - this is a static utility class
    TrackRenderer() = delete;
    
    // Helper methods
    static void drawWaveformPath(juce::Graphics& g,
                                const juce::Path& path,
                                juce::Colour colour,
                                const juce::Rectangle<int>& bounds);
};

} // namespace jucyaudio::ui