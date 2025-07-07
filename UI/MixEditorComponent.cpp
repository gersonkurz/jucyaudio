#include <Database/Includes/INavigationNode.h>
#include <UI/MixEditorComponent.h>
#include <UI/MainComponent.h>
#include <Utils/UiUtils.h>

namespace jucyaudio
{
    namespace ui
    {
        MixEditorComponent::MixEditorComponent()
        {
            m_placeholderLabel.setText("Mix Editor", juce::dontSendNotification);
            m_placeholderLabel.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(m_placeholderLabel);
        }

        void MixEditorComponent::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ListBox::backgroundColourId).darker());
        }

        void MixEditorComponent::resized()
        {
            m_placeholderLabel.setBounds(getLocalBounds());
        }
    } // namespace ui
} // namespace jucyaudio
