#include <UI/MainPlaybackAndStatusComponent.h>
#include <UI/MainComponent.h>
#include <UI/PlaybackController.h>
#include <UI/Settings.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        MainPlaybackAndStatusComponent::MainPlaybackAndStatusComponent(MainComponent &owner)
            : m_ownerMainComponent{owner},
              m_player{owner.m_enhancedPlayer}
        {
            // Set before any layout runs: getPreferredHeight() asks the player how tall the genre cloud
            // needs to be, and that depends on the padding reserved for the VU meters.
            m_player.setRightHandPadding(kVuMeterAreaWidth);
            addAndMakeVisible(m_player);
            addAndMakeVisible(m_statusBar);

            addAndMakeVisible(m_vuMeterLeft);
            addAndMakeVisible(m_vuMeterRight);
        }

        MainPlaybackAndStatusComponent::~MainPlaybackAndStatusComponent()
        {
        }

        void MainPlaybackAndStatusComponent::resized()
        {
            auto bounds = getLocalBounds();
            m_statusBar.setBounds(bounds.removeFromBottom(kStatusBarHeight));

            m_player.setRightHandPadding(kVuMeterAreaWidth);
            m_player.setBounds(bounds);

            auto vuArea = bounds.removeFromRight(kVuMeterAreaWidth);
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
