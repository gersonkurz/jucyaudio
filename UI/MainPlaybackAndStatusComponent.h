#pragma once

#include <UI/EnhancedPlayerComponent.h>
#include <UI/StatusBarComponent.h>
#include <UI/VUMeterComponent.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace jucyaudio
{
    namespace ui
    {
        // Forward declarations
        class MainComponent;
        // PlaybackController is not directly needed by MainPlaybackAndStatusComponent's interface now

        class MainPlaybackAndStatusComponent : public juce::Component, private juce::Timer
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

        private:
            void timerCallback() override;

            MainComponent &m_ownerMainComponent;
            EnhancedPlayerComponent& m_player; 
            StatusBarComponent m_statusBar;

            VUMeterComponent m_vuMeterLeft;
            VUMeterComponent m_vuMeterRight;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainPlaybackAndStatusComponent)
        };

    } // namespace ui
} // namespace jucyaudio