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
            addAndMakeVisible(m_statusBar);

            addAndMakeVisible(m_vuMeterLeft);
            addAndMakeVisible(m_vuMeterRight);

            // Timer removed - now handled by TimerMultiplexer
        }

        MainPlaybackAndStatusComponent::~MainPlaybackAndStatusComponent()
        {
            // No timer to stop
        }

        void MainPlaybackAndStatusComponent::resized()
        {
            auto bounds = getLocalBounds();
            m_statusBar.setBounds(bounds.removeFromBottom(22)); // Use a fixed height

            // Define the space for the VU meters
            constexpr int vuMeterAreaWidth = 50;

            // 1. Tell the player component to leave space for the VU meters.
            // This will trigger the player's internal `resized()`.
            m_player.setRightHandPadding(vuMeterAreaWidth);

            // 2. The player now occupies the entire top area. Its internal layout
            // will respect the padding we just set.
            m_player.setBounds(bounds);

            // 3. Place the VU meters in the padded area on the right.
            auto vuArea = bounds.removeFromRight(vuMeterAreaWidth);
            m_vuMeterLeft.setBounds(vuArea.removeFromLeft(20).reduced(0, 2));
            vuArea.removeFromLeft(5);
            m_vuMeterRight.setBounds(vuArea.removeFromLeft(20).reduced(0, 2));
        }

        void MainPlaybackAndStatusComponent::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId).darker(0.2f));
        }

        void MainPlaybackAndStatusComponent::updateVUMeters()
        {
            m_vuMeterLeft.setLevel(m_ownerMainComponent.m_playbackController.getPeakLeft());
            m_vuMeterRight.setLevel(m_ownerMainComponent.m_playbackController.getPeakRight());
        }

    } // namespace ui
} // namespace jucyaudio
