#pragma once

#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
        // Custom LookAndFeel class to fix checkbox rendering in light theme
        class CheckboxLookAndFeel final : public juce::LookAndFeel_V4
        {
        protected:
            CheckboxLookAndFeel();

        public:
            static CheckboxLookAndFeel* getInstance();
            static void releaseMemory();

        private:
            void drawToggleButton(juce::Graphics &g, juce::ToggleButton &button, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
        };

    }
}

