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
              m_player{owner.m_enhancedPlayer},
              m_visualizerFIFO{config::theSettings.audioSettings.visualizerBufferSize}
        {
            addAndMakeVisible(m_player);
            addAndMakeVisible(m_statusBar);

            addAndMakeVisible(m_vuMeterLeft);
            addAndMakeVisible(m_vuMeterRight);

            // Set up visualizer (hidden by default)
            m_visualizer.setVisualizerFIFO(&m_visualizerFIFO);
            m_visualizer.setPresetPath(ProjectMComponent::getDefaultPresetsDirectory());
            addChildComponent(m_visualizer);  // Not visible initially
        }

        MainPlaybackAndStatusComponent::~MainPlaybackAndStatusComponent()
        {
            m_visualizer.stop();
        }

        void MainPlaybackAndStatusComponent::resized()
        {
            auto bounds = getLocalBounds();
            m_statusBar.setBounds(bounds.removeFromBottom(22));

            // If visualizer is visible, allocate space for it on the right
            if (m_visualizerVisible)
            {
                auto visualizerArea = bounds.removeFromRight(kVisualizerHeight);
                m_visualizer.setBounds(visualizerArea);
            }

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

        void MainPlaybackAndStatusComponent::toggleVisualizer()
        {
            m_visualizerVisible = !m_visualizerVisible;
            spdlog::info("toggleVisualizer: setting visible to {}", m_visualizerVisible);

            m_visualizer.setVisible(m_visualizerVisible);

            if (m_visualizerVisible)
            {
                spdlog::info("toggleVisualizer: starting visualizer");
                m_visualizer.start();
            }
            else
            {
                spdlog::info("toggleVisualizer: stopping visualizer");
                m_visualizer.stop();
            }

            // Notify parent to resize
            if (auto* parent = getParentComponent())
            {
                parent->resized();
            }
        }

        int MainPlaybackAndStatusComponent::getPreferredHeight() const
        {
            return m_visualizerVisible ? kBaseHeight + kVisualizerHeight : kBaseHeight;
        }

    } // namespace ui
} // namespace jucyaudio
