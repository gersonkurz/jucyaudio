#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
        class PluginScanDialog final
        {
        public:
            static void launch(juce::Component *parentToCenterOn = nullptr);
        };
    } // namespace ui
} // namespace jucyaudio
