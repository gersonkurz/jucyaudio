#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
        class VUMeterComponent : public juce::Component
        {
        public:
            VUMeterComponent();
            ~VUMeterComponent() override;

            void paint(juce::Graphics& g) override;
            
            void setLevel(float newLevel);
            
            // Called by TimerMultiplexer instead of internal timer
            void updateDecay();
            
            // LED style configuration
            void setLedStyle(bool enabled) { m_ledStyle = enabled; repaint(); }
            bool isLedStyle() const { return m_ledStyle; }

        private:
            static constexpr float kLevelEpsilon = 0.001f;
            float m_level = 0.0f;
            float m_peak = 0.0f;
            float m_peakHoldTime = 0.0f;
            
            // LED style settings
            bool m_ledStyle = true;  // Default to LED style
            static constexpr int kNumLeds = 20;  // Number of LED segments
            static constexpr float kLedSpacing = 0.15f;  // Space between LEDs as fraction of LED height
            
            // Helper function to get LED color
            juce::Colour getLedColour(int ledIndex, int totalLeds) const;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VUMeterComponent)
        };
    }
}
