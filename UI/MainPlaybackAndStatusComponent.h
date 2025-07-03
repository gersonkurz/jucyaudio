#pragma once

#include <UI/PlaybackToolbarComponent.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace jucyaudio
{
    namespace ui
    {
        // Forward declarations
        class MainComponent;
        // PlaybackController is not directly needed by MainPlaybackAndStatusComponent's interface now

        class MainPlaybackAndStatusComponent : public juce::Component
        {
        public:
            explicit MainPlaybackAndStatusComponent(MainComponent &owner);
            ~MainPlaybackAndStatusComponent() override;

            void resized() override;
            void paint(juce::Graphics &g) override;

            void setStatusMessage(const juce::String &message, bool isError = false);

            // Provide access to the toolbar for MainComponent to wire it up
            PlaybackToolbarComponent &getPlaybackToolbar()
            {
                return m_playbackToolbar;
            }
            const PlaybackToolbarComponent &getPlaybackToolbar() const
            {
                return m_playbackToolbar;
            }

        private:
            MainComponent &m_ownerMainComponent;
            PlaybackToolbarComponent& m_playbackToolbar; 
            juce::Label m_statusLabel;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainPlaybackAndStatusComponent)
        };

    } // namespace ui
} // namespace jucyaudio