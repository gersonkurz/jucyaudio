#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace jucyaudio
{
    namespace ui
    {
        class StatusBarComponent : public juce::Component, private juce::Timer
        {
        public:
            StatusBarComponent();
            ~StatusBarComponent() override;

            void paint(juce::Graphics& g) override;
            void resized() override;
            void lookAndFeelChanged() override;

            void setInfoMessage(const juce::String& message);
            void postMessage(const juce::String& message, bool isError = false);

        private:
            void timerCallback() override;

            juce::Label m_infoLabel;
            juce::Label m_messageLabel;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBarComponent)
        };
    } // namespace ui
} // namespace jucyaudio
