#include <UI/MainPlaybackAndStatusComponent.h>
#include <UI/MainComponent.h>

namespace jucyaudio
{
    namespace ui
    {

        MainPlaybackAndStatusComponent::MainPlaybackAndStatusComponent(MainComponent &owner)
            : m_ownerMainComponent{owner},
              m_player{owner.m_enhancedPlayer}
        {
            // Add and make visible the child components
            addAndMakeVisible(m_player);

            m_statusLabel.setText("", juce::dontSendNotification);
            m_statusLabel.setJustificationType(juce::Justification::centredLeft);
            m_statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey); // Example initial color
            addAndMakeVisible(m_statusLabel);
        }

        MainPlaybackAndStatusComponent::~MainPlaybackAndStatusComponent()
        {
            // Child components (m_player, m_statusLabel) are destroyed automatically
        }

        void MainPlaybackAndStatusComponent::paint(juce::Graphics &g)
        {
            // Fill background for the entire panel
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId).darker(0.15f)); // Slightly darker than toolbar

            // Optional: Draw a top border line to separate from the content above
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.drawLine(0.0f, 0.0f, static_cast<float>(getWidth()), 0.0f, 1.0f);
        }

        void MainPlaybackAndStatusComponent::resized()
        {
            auto bounds = getLocalBounds();
            const int padding = 5;
            const int statusHeight = 25;

            // Status label at the bottom
            auto statusBounds = bounds.removeFromBottom(statusHeight);
            m_statusLabel.setBounds(statusBounds.reduced(padding, 2));

            // Enhanced player takes the remaining space
            m_player.setBounds(bounds);

            /*
            // Alternative layout: Status label on left, toolbar on right
            int statusWidth = 200; // Example width for status label
            if (bounds.getWidth() > statusWidth + padding)
            {
                m_statusLabel.setBounds(bounds.removeFromLeft(statusWidth).reduced(padding));
                m_playbackToolbar.setBounds(bounds.reduced(padding, 0)); // Toolbar takes remaining width
            }
            else // Not enough space for side-by-side
            {
                m_playbackToolbar.setBounds(bounds.removeFromTop(toolbarHeight).reduced(padding,0));
                m_statusLabel.setBounds(bounds.reduced(padding));
            }
            */
        }

        void MainPlaybackAndStatusComponent::setStatusMessage(const juce::String &message, bool isError)
        {
            m_statusLabel.setText(message, juce::dontSendNotification);
            if (isError)
            {
                m_statusLabel.setColour(juce::Label::textColourId, juce::Colours::red);
            }
            else
            {
                // Reset to default status color (could be a LookAndFeel color)
                m_statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
            }
        }

        // Optional getter, already in header:
        // EnhancedPlayerComponent& MainPlaybackAndStatusComponent::getPlayer() { return m_player; }

    } // namespace ui
} // namespace jucyaudio