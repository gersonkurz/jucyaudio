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
            m_statusBar.setBounds(bounds.removeFromBottom(22));

            // Define the space for the VU meters
            constexpr int vuMeterAreaWidth = 50;

            m_player.setRightHandPadding(vuMeterAreaWidth);
            m_player.setBounds(bounds);

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
