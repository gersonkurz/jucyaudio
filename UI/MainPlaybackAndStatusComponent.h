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

            /// @brief Height this panel wants when laid out at the given width.
            /// @param availableWidth The width the panel is about to be given. Must be passed in rather
            ///        than read from getWidth(): the owner asks for the height before assigning bounds,
            ///        so our own width is still the previous one during a resize.
            /// @return Preferred height in pixels.
            int getPreferredHeight(int availableWidth) const
            {
                if (!m_genreCloudVisible)
                {
                    return kBaseHeight;
                }
                // The cloud wraps, so its height depends on the width it gets.
                return juce::jmax(kBaseHeight, m_player.getRequiredHeightForGenreCloud(availableWidth) + kStatusBarHeight);
            }

            // Show the genre cloud instead of the (hidden) waveform while editing a mix.
            void setGenreCloudVisible(bool shouldBeVisible)
            {
                if (m_genreCloudVisible != shouldBeVisible)
                {
                    m_genreCloudVisible = shouldBeVisible;
                    m_player.setGenreCloudVisible(shouldBeVisible);
                }
            }

            GenreCloudComponent &getGenreCloud()
            {
                return m_player.getGenreCloud();
            }

        private:
            MainComponent &m_ownerMainComponent;
            EnhancedPlayerComponent& m_player;
            StatusBarComponent m_statusBar;

            bool m_genreCloudVisible{false};

            VUMeterComponent m_vuMeterLeft;
            VUMeterComponent m_vuMeterRight;

            static constexpr int kBaseHeight = 120;
            static constexpr int kStatusBarHeight = 22;
            static constexpr int kVuMeterAreaWidth = 50;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainPlaybackAndStatusComponent)
        };

    } // namespace ui
} // namespace jucyaudio