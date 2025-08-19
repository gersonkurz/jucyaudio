#include "VUMeterComponent.h"
#include <algorithm>
#include <cmath>

namespace jucyaudio
{
    namespace ui
    {
        VUMeterComponent::VUMeterComponent()
        {
            // Timer removed - now handled by TimerMultiplexer
        }

        VUMeterComponent::~VUMeterComponent()
        {
            // No timer to stop
        }

        void VUMeterComponent::paint(juce::Graphics& g)
        {
            g.fillAll(juce::Colours::black);

            auto bounds = getLocalBounds().toFloat();
            g.setColour(juce::Colours::darkgrey);
            g.drawRect(bounds, 1.0f);

            bounds.reduce(3, 3);

            if (m_ledStyle)
            {
                // LED-style segmented meter
                const float totalHeight = bounds.getHeight();
                const float ledHeight = totalHeight / (kNumLeds + (kNumLeds - 1) * kLedSpacing);
                const float gapHeight = ledHeight * kLedSpacing;
                
                // Ensure level is valid
                const float safeLevel = std::isfinite(m_level) ? std::clamp(m_level, 0.0f, 1.0f) : 0.0f;
                const int numLitLeds = static_cast<int>(safeLevel * kNumLeds);
                
                // Draw LEDs from bottom to top
                for (int i = 0; i < kNumLeds; ++i)
                {
                    const float ledY = bounds.getBottom() - (i + 1) * (ledHeight + gapHeight) + gapHeight;
                    const juce::Rectangle<float> ledRect(bounds.getX(), ledY, bounds.getWidth(), ledHeight);
                    
                    // Determine if this LED should be lit
                    const bool isLit = i < numLitLeds;
                    
                    if (isLit)
                    {
                        // Get the appropriate color for this LED
                        g.setColour(getLedColour(i, kNumLeds));
                        g.fillRoundedRectangle(ledRect, 1.0f);
                        
                        // Add a subtle glow effect for lit LEDs
                        g.setColour(getLedColour(i, kNumLeds).brighter(0.3f));
                        g.drawRoundedRectangle(ledRect.reduced(1), 1.0f, 0.5f);
                    }
                    else
                    {
                        // Draw unlit LED (dark version of the color it would be)
                        g.setColour(getLedColour(i, kNumLeds).darker(0.85f));
                        g.fillRoundedRectangle(ledRect, 1.0f);
                    }
                }
                
                // Draw peak indicator if we have a peak hold
                if (m_peak > 0.0f && m_peakHoldTime > 0.0f)
                {
                    const int peakLed = static_cast<int>(m_peak * kNumLeds) - 1;
                    if (peakLed >= 0 && peakLed < kNumLeds)
                    {
                        const float ledY = bounds.getBottom() - (peakLed + 1) * (ledHeight + gapHeight) + gapHeight;
                        const juce::Rectangle<float> peakRect(bounds.getX(), ledY, bounds.getWidth(), ledHeight);
                        
                        // Draw peak LED with bright white outline
                        g.setColour(juce::Colours::white.withAlpha(0.9f));
                        g.drawRoundedRectangle(peakRect, 1.0f, 2.0f);
                    }
                }
            }
            else
            {
                // Original continuous bar style
                const auto totalHeight = bounds.getHeight();
                const auto barWidth = bounds.getWidth();
                const auto barX = bounds.getX();
                const auto barY = bounds.getY();

                // Define the color sections
                const float redThreshold = 0.85f;
                const float yellowThreshold = 0.70f;

                const auto redHeight = totalHeight * (1.0f - redThreshold);
                const auto yellowHeight = totalHeight * (redThreshold - yellowThreshold);
                const auto greenHeight = totalHeight * yellowThreshold;

                const auto yellowY = barY + redHeight;
                const auto greenY = yellowY + yellowHeight;

                // Draw the colored sections
                g.setColour(juce::Colours::red);
                g.fillRect(barX, barY, barWidth, redHeight);

                g.setColour(juce::Colours::yellow);
                g.fillRect(barX, yellowY, barWidth, yellowHeight);

                g.setColour(juce::Colours::green);
                g.fillRect(barX, greenY, barWidth, greenHeight);

                // Draw the "unlit" part
                const float safeLevel = std::isfinite(m_level) ? std::clamp(m_level, 0.0f, 1.0f) : 0.0f;
                const float unlitHeight = totalHeight * (1.0f - safeLevel);
                
                if (unlitHeight > 0.0f && std::isfinite(unlitHeight))
                {
                    g.setColour(juce::Colours::black);
                    g.fillRect(barX, barY, barWidth, unlitHeight);
                }
            }
        }

        void VUMeterComponent::setLevel(float newLevel)
        {
            // Validate the input to prevent crashes from NaN or infinite values
            if (!std::isfinite(newLevel))
            {
                m_level = 0.0f;
                return;
            }
            
            // Clamp the level to valid range [0.0, 1.0]
            m_level = std::clamp(newLevel, 0.0f, 1.0f);
            
            if (m_level > m_peak)
            {
                m_peak = m_level;
                m_peakHoldTime = 2.0f; // Hold peak for 2 seconds
            }
            repaint();
        }

        void VUMeterComponent::updateDecay()
        {
            bool needsRepaint = false;
            
            // Decay the main level
            if (m_level > 0)
            {
                m_level *= 0.7f; // Decay factor
                if (m_level < 0.001f)
                {
                    m_level = 0.0f;
                }
                needsRepaint = true;
            }
            
            // Decay the peak hold time
            if (m_peakHoldTime > 0.0f)
            {
                m_peakHoldTime -= 0.05f; // Assuming 20Hz update rate
                if (m_peakHoldTime <= 0.0f)
                {
                    m_peak = m_level; // Reset peak to current level
                    m_peakHoldTime = 0.0f;
                }
                needsRepaint = true;
            }
            
            if (needsRepaint)
            {
                repaint();
            }
        }
        
        juce::Colour VUMeterComponent::getLedColour(int ledIndex, int totalLeds) const
        {
            // Calculate position as a fraction (0.0 = bottom, 1.0 = top)
            const float position = static_cast<float>(ledIndex) / static_cast<float>(totalLeds - 1);
            
            // Define thresholds matching the original design
            const float redThreshold = 0.85f;    // Top 15% are red
            const float yellowThreshold = 0.70f; // Next 15% are yellow
            // Bottom 70% are green
            
            if (position >= redThreshold)
            {
                // Red zone - gradient from orange-red to bright red
                const float redIntensity = (position - redThreshold) / (1.0f - redThreshold);
                return juce::Colour::fromRGB(
                    255,
                    static_cast<uint8_t>(80 - redIntensity * 60),  // Reduce green component
                    0
                );
            }
            else if (position >= yellowThreshold)
            {
                // Yellow zone - gradient from yellow-green to yellow-orange
                const float yellowIntensity = (position - yellowThreshold) / (redThreshold - yellowThreshold);
                return juce::Colour::fromRGB(
                    static_cast<uint8_t>(200 + yellowIntensity * 55),  // Increase red
                    static_cast<uint8_t>(230 - yellowIntensity * 30),  // Slight decrease in green
                    0
                );
            }
            else
            {
                // Green zone - gradient from dark green to bright green
                const float greenIntensity = position / yellowThreshold;
                return juce::Colour::fromRGB(
                    0,
                    static_cast<uint8_t>(100 + greenIntensity * 155),  // From dark to bright green
                    static_cast<uint8_t>(20 * (1.0f - greenIntensity)) // Slight blue tint at bottom
                );
            }
        }
    }
}
