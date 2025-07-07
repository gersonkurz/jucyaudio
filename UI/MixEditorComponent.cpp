#include <Database/Includes/INavigationNode.h>
#include <UI/MainComponent.h>
#include <UI/MixEditorComponent.h>
#include <Utils/UiUtils.h>

namespace jucyaudio
{
    namespace ui
    {
        MixEditorComponent::MixEditorComponent()
            : m_timeline{m_formatManager, m_thumbnailCache}
        {
            m_formatManager.registerBasicFormats();

            addAndMakeVisible(m_timeline);
        }

        void MixEditorComponent::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ListBox::backgroundColourId).darker());
        }

        void MixEditorComponent::loadMix(MixId mixId)
        {
            spdlog::info("Loading mix with ID: {}", mixId);
            m_mixProjectLoader.loadMix(mixId);
            m_timeline.populateFrom(m_mixProjectLoader);
            m_timeline.repaint(); // Ensure the timeline is updated with the new mix data
        }

        void MixEditorComponent::resized()
        {
        }
    } // namespace ui
} // namespace jucyaudio
