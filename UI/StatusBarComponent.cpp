#include <UI/StatusBarComponent.h>

namespace jucyaudio
{
    namespace ui
    {
        StatusBarComponent::StatusBarComponent()
        {
            addAndMakeVisible(m_infoLabel);
            m_infoLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
            m_infoLabel.setJustificationType(juce::Justification::centredLeft);

            addAndMakeVisible(m_messageLabel);
            m_messageLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
            m_messageLabel.setJustificationType(juce::Justification::centredRight);
        }

        StatusBarComponent::~StatusBarComponent()
        {
            stopTimer();
        }

        void StatusBarComponent::resized()
        {
            auto bounds = getLocalBounds();
            // Give temp message 1/3 of the space on the right, and the rest to the info label
            m_messageLabel.setBounds(bounds.removeFromRight(bounds.getWidth() / 3));
            m_infoLabel.setBounds(bounds);
        }

        void StatusBarComponent::setInfoMessage(const juce::String& message)
        {
            m_infoLabel.setText(message, juce::dontSendNotification);
        }

        void StatusBarComponent::postMessage(const juce::String& message, bool isError)
        {
            m_messageLabel.setText(message, juce::dontSendNotification);
            m_messageLabel.setColour(juce::Label::textColourId, isError ? juce::Colours::red : juce::Colours::lightgrey);
            startTimer(3000); // Message will be cleared after 3 seconds
        }

        void StatusBarComponent::timerCallback()
        {
            stopTimer();
            m_messageLabel.setText("", juce::dontSendNotification);
        }

    } // namespace ui
} // namespace jucyaudio
