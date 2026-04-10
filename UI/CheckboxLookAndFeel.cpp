#include <UI/CheckboxLookAndFeel.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {

        CheckboxLookAndFeel::CheckboxLookAndFeel()
        {
            // Colors are resolved dynamically in drawToggleButton() from the active default look-and-feel.
        }

        std::unique_ptr<CheckboxLookAndFeel> s_instance;

        CheckboxLookAndFeel *CheckboxLookAndFeel::getInstance()
        {
            if (!s_instance)
            {
                s_instance = std::make_unique<CheckboxLookAndFeel>();
            }
            return s_instance.get();
        }

        void CheckboxLookAndFeel::releaseMemory()
        {
            if (s_instance)
            {
                s_instance.reset();
            }
        }

        void CheckboxLookAndFeel::drawToggleButton(
            juce::Graphics &g, juce::ToggleButton &button, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
        {
            const auto &defaultLf = juce::LookAndFeel::getDefaultLookAndFeel();
            const auto textColour = defaultLf.findColour(juce::ToggleButton::textColourId);

            const auto fontSize = juce::jmin(15.0f, button.getHeight() * 0.75f);
            const auto tickWidth = fontSize * 1.1f;

            // Subtle hover/pressed background feedback
            if (button.isEnabled() && (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown))
            {
                g.setColour(textColour.withAlpha(shouldDrawButtonAsDown ? 0.1f : 0.05f));
                g.fillRect(button.getLocalBounds().toFloat());
            }

            // Draw checkbox outline (the missing piece in light theme!)
            juce::Rectangle<float> tickBounds(4.0f, (button.getHeight() - tickWidth) * 0.5f, tickWidth, tickWidth);

            // Use a contrasting color for the box outline
            g.setColour(textColour.withAlpha(0.8f));
            g.drawRect(tickBounds, 1.0f);

            // Fill if checked — use the text colour for the tick so it is always
            // visible regardless of theme (tickColourId is not set by the theme)
            if (button.getToggleState())
            {
                g.setColour(textColour.withAlpha(button.isEnabled() ? 1.0f : 0.4f));
                const auto tick = tickBounds.reduced(tickWidth * 0.25f);

                // Draw checkmark
                juce::Path p;
                p.startNewSubPath(tick.getX(), tick.getCentreY());
                p.lineTo(tick.getCentreX(), tick.getBottom());
                p.lineTo(tick.getRight(), tick.getY());

                g.strokePath(p, juce::PathStrokeType(2.0f));
            }

            // Draw text
            g.setColour(textColour);
            g.setFont(fontSize);

            if (!button.isEnabled())
                g.setOpacity(0.5f);

            g.drawFittedText(button.getButtonText(),
                button.getLocalBounds().withTrimmedLeft(juce::roundToInt(tickWidth) + 10).withTrimmedRight(2),
                juce::Justification::centredLeft,
                10);
        }

    } // namespace ui
} // namespace jucyaudio
