#include "VUMeterComponent.h"

namespace jucyaudio
{
    namespace ui
    {
        VUMeterComponent::VUMeterComponent()
        {
            startTimerHz(25);
        }

        VUMeterComponent::~VUMeterComponent()
        {
            stopTimer();
        }

        void VUMeterComponent::paint(juce::Graphics& g)
        {
            g.fillAll(juce::Colours::black);

            auto bounds = getLocalBounds().toFloat();
            g.setColour(juce::Colours::darkgrey);
            g.drawRect(bounds, 1.0f);

            bounds.reduce(2, 2);

            const auto totalHeight = bounds.getHeight();
            const auto barWidth = bounds.getWidth();
            const auto barX = bounds.getX();
            const auto barY = bounds.getY();

            // Define the color sections. The meter is drawn from top (1.0) to bottom (0.0).
            const float redThreshold = 0.85f;
            const float yellowThreshold = 0.70f;

            const auto redHeight = totalHeight * (1.0f - redThreshold); // Top 15%
            const auto yellowHeight = totalHeight * (redThreshold - yellowThreshold); // Middle 15%
            const auto greenHeight = totalHeight * yellowThreshold; // Bottom 70%

            const auto yellowY = barY + redHeight;
            const auto greenY = yellowY + yellowHeight;

            // Draw the colored sections of the meter bar
            g.setColour(juce::Colours::red);
            g.fillRect(barX, barY, barWidth, redHeight);

            g.setColour(juce::Colours::yellow);
            g.fillRect(barX, yellowY, barWidth, yellowHeight);

            g.setColour(juce::Colours::green);
            g.fillRect(barX, greenY, barWidth, greenHeight);

            // Draw the "unlit" part of the meter on top, covering the colored sections
            const float unlitHeight = totalHeight * (1.0f - m_level);
            g.setColour(juce::Colours::black);
            g.fillRect(barX, barY, barWidth, unlitHeight);
        }

        void VUMeterComponent::setLevel(float newLevel)
        {
            m_level = newLevel;
            if (m_level > m_peak)
            {
                m_peak = m_level;
            }
            repaint();
        }

        void VUMeterComponent::timerCallback()
        {
            if (m_level > 0)
            {
                m_level *= 0.7f; // Decay factor
                if (m_level < 0.001f)
                {
                    m_level = 0.0f;
                }
                repaint();
            }
        }
    }
}
