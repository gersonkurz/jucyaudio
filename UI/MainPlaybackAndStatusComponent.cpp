#include <UI/MainPlaybackAndStatusComponent.h>
#include <UI/MainComponent.h>
#include <UI/PlaybackController.h>

namespace jucyaudio
{
    namespace ui
    {
        MainPlaybackAndStatusComponent::MainPlaybackAndStatusComponent(MainComponent &owner)
            : m_ownerMainComponent{owner},
              m_player{owner.m_enhancedPlayer}
        {
            addAndMakeVisible(m_player);
            addAndMakeVisible(m_statusLabel);
            m_statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

            addAndMakeVisible(m_vuMeterLeft);
            addAndMakeVisible(m_vuMeterRight);

            startTimerHz(25);
        }

        MainPlaybackAndStatusComponent::~MainPlaybackAndStatusComponent()
        {
            stopTimer();
        }

        void MainPlaybackAndStatusComponent::resized()
        {
            auto bounds = getLocalBounds();
            m_statusLabel.setBounds(bounds.removeFromBottom(20).reduced(5, 0));
            
            auto vuArea = bounds.removeFromRight(50);
            m_vuMeterLeft.setBounds(vuArea.removeFromLeft(20).reduced(0, 2));
            vuArea.removeFromLeft(5);
            m_vuMeterRight.setBounds(vuArea.removeFromLeft(20).reduced(0, 2));

            m_player.setBounds(bounds);
        }

        void MainPlaybackAndStatusComponent::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId).darker(0.2f));
        }

        void MainPlaybackAndStatusComponent::setStatusMessage(const juce::String &message, bool isError)
        {
            m_statusLabel.setText(message, juce::dontSendNotification);
            m_statusLabel.setColour(juce::Label::textColourId, isError ? juce::Colours::red : juce::Colours::lightgrey);
        }

        void MainPlaybackAndStatusComponent::timerCallback()
        {
            m_vuMeterLeft.setLevel(m_ownerMainComponent.m_playbackController.getPeakLeft());
            m_vuMeterRight.setLevel(m_ownerMainComponent.m_playbackController.getPeakRight());
        }

    } // namespace ui
} // namespace jucyaudio
