#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
        class JucyLookAndFeel : public juce::LookAndFeel_V4
        {
        public:
            JucyLookAndFeel() = default;
            ~JucyLookAndFeel() override = default;

            // Override drawToggleButton to fix checkbox rendering in all themes
            void drawToggleButton(juce::Graphics &g, juce::ToggleButton &button, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
        };
    } // namespace ui
} // namespace jucyaudio