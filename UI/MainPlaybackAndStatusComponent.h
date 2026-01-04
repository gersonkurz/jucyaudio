#pragma once

#include <UI/EnhancedPlayerComponent.h>
#include <UI/StatusBarComponent.h>
#include <UI/VUMeterComponent.h>
#include <UI/Visualizer/ProjectMComponent.h>
#include <Audio/AudioVisualizerFIFO.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace jucyaudio
{
    namespace ui
    {
        // Forward declarations
        class MainComponent;

        class MainPlaybackAndStatusComponent : public juce::Component
        {
        public:
            explicit MainPlaybackAndStatusComponent(MainComponent &owner);
            ~MainPlaybackAndStatusComponent() override;

            void resized() override;
            void paint(juce::Graphics &g) override;

            StatusBarComponent &getStatusBar()
            {
                return m_statusBar;
            }

            // Provide access to the player for MainComponent to wire it up
            EnhancedPlayerComponent &getPlayer()
            {
                return m_player;
            }
            const EnhancedPlayerComponent &getPlayer() const
            {
                return m_player;
            }

            // Called by TimerMultiplexer to update VU meters
            void updateVUMeters();

            VUMeterComponent& getVUMeterLeft() { return m_vuMeterLeft; }
            VUMeterComponent& getVUMeterRight() { return m_vuMeterRight; }

            // Visualizer control
            void toggleVisualizer();
            bool isVisualizerVisible() const { return m_visualizerVisible; }
            audio::AudioVisualizerFIFO* getVisualizerFIFO() { return &m_visualizerFIFO; }

            // Get preferred height based on visualizer state
            int getPreferredHeight() const;

        private:
            MainComponent &m_ownerMainComponent;
            EnhancedPlayerComponent& m_player;
            StatusBarComponent m_statusBar;

            VUMeterComponent m_vuMeterLeft;
            VUMeterComponent m_vuMeterRight;

            // Visualizer components
            audio::AudioVisualizerFIFO m_visualizerFIFO;
            ProjectMComponent m_visualizer;
            bool m_visualizerVisible{false};

            static constexpr int kBaseHeight = 120;
            static constexpr int kVisualizerHeight = 200;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainPlaybackAndStatusComponent)
        };

    } // namespace ui
} // namespace jucyaudio