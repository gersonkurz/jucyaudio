#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/MixMarker.h>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

namespace jucyaudio
{
    namespace ui
    {
        class MarkerRulerComponent : public juce::Component, public juce::TooltipClient
        {
        public:
            MarkerRulerComponent();
            ~MarkerRulerComponent() override = default;

            void paint(juce::Graphics &g) override;
            void resized() override;
            void mouseDown(const juce::MouseEvent &event) override;
            void mouseMove(const juce::MouseEvent &event) override;
            void mouseExit(const juce::MouseEvent &event) override;

            // TooltipClient
            juce::String getTooltip() override
            {
                return m_currentTooltip;
            }

            // Set the markers to display
            void setMarkers(const std::vector<database::MixMarker> &markers);

            // Set the total duration of the mix (needed for positioning)
            void setMixDuration(std::chrono::milliseconds duration);

            // Set the current playback position (for visual feedback)
            void setPlaybackPosition(double positionMs);

            // Callbacks
            std::function<void(std::chrono::milliseconds position)> onMarkerAdded;
            std::function<void(MarkerId markerId)> onMarkerClicked;

            // Constants
            static constexpr int RULER_HEIGHT = 30;

        private:
            std::optional<database::MixMarker> findMarkerAtPosition(int xPos) const;
            int getXForTime(std::chrono::milliseconds time) const;
            std::chrono::milliseconds getTimeForX(int xPos) const;

            std::vector<database::MixMarker> m_markers;
            std::chrono::milliseconds m_mixDuration{0};
            double m_playbackPosition{0.0};
            std::optional<MarkerId> m_hoveredMarkerId;
            juce::String m_currentTooltip;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarkerRulerComponent)
        };
    } // namespace ui
} // namespace jucyaudio