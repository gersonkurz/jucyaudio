#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace jucyaudio
{
    namespace ui
    {
        // This class is our splash screen. It inherits from the JUCE class
        // and overrides the paint method to draw our logo.
        class JucyAudioSplashScreen : public juce::SplashScreen
        {
        public:
            JucyAudioSplashScreen();

            // This is where we'll draw the logo.
            void paint(juce::Graphics &g) override;

        private:
            juce::Image m_logo;
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JucyAudioSplashScreen)
        };
    } // namespace ui
} // namespace jucyaudio