#include <UI/MainPlaybackAndStatusComponent.h>
#include <UI/MainComponent.h>

namespace jucyaudio
{
    namespace ui
    {

        MainPlaybackAndStatusComponent::MainPlaybackAndStatusComponent(MainComponent &owner)
            : m_ownerMainComponent{owner},
              m_playbackToolbar{owner.m_playbackToolbar}
        // m_playbackToolbar is default-constructed as a member
        // m_statusLabel is default-constructed
        {
            // Add and make visible the child components
            addAndMakeVisible(m_playbackToolbar);

            m_statusLabel.setText("", juce::dontSendNotification);
            m_statusLabel.setJustificationType(juce::Justification::centredLeft);
            m_statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey); // Example initial color
            addAndMakeVisible(m_statusLabel);
        }

        MainPlaybackAndStatusComponent::~MainPlaybackAndStatusComponent()
        {
            // Child components (m_playbackToolbar, m_statusLabel) are destroyed automatically
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
            int padding = 5; // Padding around elements

            // Example layout:
            // Status label on the left, playback toolbar takes the rest of the space.
            // Or, playback toolbar at the top, status label at the bottom of this panel.

            // Let's try: PlaybackToolbar fills most of it, status label takes a small height at the bottom.
            // This assumes PlaybackToolbarComponent is designed to look good with a certain height.
            // The old MainComponent gave it 30px height.

            int toolbarHeight = 30;                                               // Typical height for a compact toolbar
            int statusLabelHeight = bounds.getHeight() - toolbarHeight - padding; // Remaining height for status, or fixed

            if (statusLabelHeight < 15 && bounds.getHeight() > toolbarHeight) // Ensure some minimum if space allows
            {
                statusLabelHeight = 15;
            }
            else if (bounds.getHeight() <= toolbarHeight) // Not enough space for status label below
            {
                statusLabelHeight = 0;
            }

            // Playback toolbar at the top of this panel's area
            m_playbackToolbar.setBounds(bounds.removeFromTop(toolbarHeight).reduced(padding, 0));

            // Status label below it, if there's space
            if (statusLabelHeight > 0)
            {
                m_statusLabel.setBounds(bounds.reduced(padding));
            }
            else
            {
                m_statusLabel.setBounds({}); // Collapse if no space
            }

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
        // PlaybackToolbarComponent& MainPlaybackAndStatusComponent::getPlaybackToolbar() { return m_playbackToolbar; }

    } // namespace ui
} // namespace jucyaudio