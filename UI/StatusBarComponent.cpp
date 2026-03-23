#include <UI/StatusBarComponent.h>

namespace jucyaudio
{
    namespace ui
    {
        StatusBarComponent::StatusBarComponent()
        {
            addAndMakeVisible(m_infoLabel);
            m_infoLabel.setJustificationType(juce::Justification::centredLeft);
            m_infoLabel.setBorderSize(juce::BorderSize<int>{0, 8, 0, 8});
            m_infoLabel.setMinimumHorizontalScale(0.8f);
            m_infoLabel.setInterceptsMouseClicks(false, false);

            addAndMakeVisible(m_messageLabel);
            m_messageLabel.setJustificationType(juce::Justification::centredRight);
            m_messageLabel.setBorderSize(juce::BorderSize<int>{0, 8, 0, 8});
            m_messageLabel.setMinimumHorizontalScale(0.8f);
            m_messageLabel.setInterceptsMouseClicks(false, false);

            lookAndFeelChanged();
        }

        StatusBarComponent::~StatusBarComponent()
        {
            stopTimer();
        }

        void StatusBarComponent::paint(juce::Graphics& g)
        {
            const auto background = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId).brighter(0.05f);
            g.fillAll(background);

            g.setColour(background.contrasting(0.2f).withAlpha(0.5f));
            g.drawLine(0.0f, 0.5f, static_cast<float>(getWidth()), 0.5f, 1.0f);
        }

        void StatusBarComponent::resized()
        {
            auto bounds = getLocalBounds().reduced(4, 0);
            // Give temp message 1/3 of the space on the right, and the rest to the info label
            m_messageLabel.setBounds(bounds.removeFromRight(bounds.getWidth() / 3));
            m_infoLabel.setBounds(bounds);
        }

        void StatusBarComponent::lookAndFeelChanged()
        {
            const auto textColour = getLookAndFeel().findColour(juce::Label::textColourId);
            const auto font = juce::Font{juce::FontOptions{}.withHeight(13.0f)};

            m_infoLabel.setColour(juce::Label::textColourId, textColour);
            m_infoLabel.setFont(font);

            m_messageLabel.setColour(juce::Label::textColourId, textColour.withAlpha(0.9f));
            m_messageLabel.setFont(font);

            repaint();
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
