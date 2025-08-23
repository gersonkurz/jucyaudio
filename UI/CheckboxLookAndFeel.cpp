#include <UI/CheckboxLookAndFeel.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {

        CheckboxLookAndFeel::CheckboxLookAndFeel()
        {
            // Copy colors from parent - TODO: use from theme instead
            setColour(juce::ToggleButton::textColourId, juce::Colours::black);
            setColour(juce::ToggleButton::tickColourId, juce::Colours::black);
            setColour(juce::ToggleButton::tickDisabledColourId, juce::Colours::grey);
        }

        std::unique_ptr<CheckboxLookAndFeel> s_instance;

        CheckboxLookAndFeel *CheckboxLookAndFeel::getInstance()
        {
            if (!s_instance)
            {
                s_instance = std::unique_ptr<CheckboxLookAndFeel>(new CheckboxLookAndFeel{});
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
            const auto fontSize = juce::jmin(15.0f, button.getHeight() * 0.75f);
            const auto tickWidth = fontSize * 1.1f;

            // Draw checkbox outline (the missing piece in light theme!)
            juce::Rectangle<float> tickBounds(4.0f, (button.getHeight() - tickWidth) * 0.5f, tickWidth, tickWidth);

            // Use a contrasting color for the box outline
            g.setColour(button.findColour(juce::ToggleButton::textColourId).withAlpha(0.8f));
            g.drawRect(tickBounds, 1.0f);

            // Fill if checked
            if (button.getToggleState())
            {
                g.setColour(button.findColour(juce::ToggleButton::tickColourId));
                const auto tick = tickBounds.reduced(tickWidth * 0.25f);

                // Draw checkmark
                juce::Path p;
                p.startNewSubPath(tick.getX(), tick.getCentreY());
                p.lineTo(tick.getCentreX(), tick.getBottom());
                p.lineTo(tick.getRight(), tick.getY());

                g.strokePath(p, juce::PathStrokeType(2.0f));
            }

            // Draw text
            g.setColour(button.findColour(juce::ToggleButton::textColourId));
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
