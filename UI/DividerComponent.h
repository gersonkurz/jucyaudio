#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace jucyaudio
{
    namespace ui
    {
        class MainComponent; // Forward declaration of the owner

        class DividerComponent : public juce::Component
        {
        public:
            DividerComponent(MainComponent &owner, bool isVerticalLayout); // Constructor
            ~DividerComponent() override;

            void paint(juce::Graphics &g) override;

            void mouseEnter(const juce::MouseEvent& event) override;
            void mouseExit(const juce::MouseEvent& event) override;
            void mouseDown(const juce::MouseEvent& event) override;
            void mouseDrag(const juce::MouseEvent& event) override;
            void mouseUp(const juce::MouseEvent& event) override;

        private:
            MainComponent &m_ownerMainComponent;
            bool m_isVerticalLayout; // True if it's a vertical divider (splits horizontally)

            // For Step 2:
            bool m_isMouseDown{false};
            juce::ComponentDragger m_dragger; // For handling drag
            juce::Point<int> m_startDragPosition;
            int m_originalNavPanelWidthAtDragStart{0};

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DividerComponent)
        };

    } // namespace ui
} // namespace jucyaudio