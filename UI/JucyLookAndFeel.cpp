#include "JucyLookAndFeel.h"
#include <spdlog/spdlog.h>

namespace jucyaudio::ui
{
    void JucyLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                          bool shouldDrawButtonAsHighlighted, 
                                          bool shouldDrawButtonAsDown)
    {
        juce::ignoreUnused(shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
        
        spdlog::debug("JucyLookAndFeel::drawToggleButton called for: {}", 
                     button.getButtonText().toStdString());
        
        const auto fontSize = juce::jmin(15.0f, button.getHeight() * 0.75f);
        const auto tickWidth = fontSize * 1.1f;
        
        // Draw checkbox outline using the text color for proper theme support
        juce::Rectangle<float> tickBounds(4.0f, (button.getHeight() - tickWidth) * 0.5f, tickWidth, tickWidth);
        
        // Use text color for the checkbox outline
        g.setColour(button.findColour(juce::ToggleButton::textColourId).withAlpha(0.8f));
        g.drawRect(tickBounds, 1.0f);
        
        // Draw tick/checkmark if toggled
        if (button.getToggleState())
        {
            // Use text color for the tick as well (not a separate tick color that might not be themed properly)
            g.setColour(button.findColour(juce::ToggleButton::textColourId));
            const auto tick = tickBounds.reduced(tickWidth * 0.25f);
            
            // Draw a checkmark path
            juce::Path tickPath;
            tickPath.startNewSubPath(tick.getX(), tick.getCentreY());
            tickPath.lineTo(tick.getX() + tick.getWidth() * 0.33f, tick.getBottom());
            tickPath.lineTo(tick.getRight(), tick.getY());
            
            g.strokePath(tickPath, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
        }
        
        // Draw button text
        g.setColour(button.findColour(juce::ToggleButton::textColourId));
        g.setFont(fontSize);
        
        const auto textX = tickBounds.getRight() + 8.0f;
        const auto textWidth = button.getWidth() - textX - 2.0f;
        
        g.drawFittedText(button.getButtonText(),
                        static_cast<int>(textX), 0, static_cast<int>(textWidth), button.getHeight(),
                        juce::Justification::centredLeft, 2);
    }
}