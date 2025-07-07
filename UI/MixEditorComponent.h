#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/TrackLibrary.h>
#include <UI/DynamicColumnManager.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
        class MixEditorComponent : public juce::Component
        {
        public:
            MixEditorComponent();

            void paint(juce::Graphics &g) override;
            void resized() override;

        private:

            juce::Label m_placeholderLabel;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixEditorComponent)
        };

    } // namespace ui
} // namespace jucyaudio
