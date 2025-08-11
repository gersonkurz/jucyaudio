#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
        class VUMeterComponent : public juce::Component, private juce::Timer
        {
        public:
            VUMeterComponent();
            ~VUMeterComponent() override;

            void paint(juce::Graphics& g) override;
            
            void setLevel(float newLevel);

        private:
            void timerCallback() override;

            float m_level = 0.0f;
            float m_peak = 0.0f;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VUMeterComponent)
        };
    }
}
