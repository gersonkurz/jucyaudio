#include "MarkerRulerComponent.h"
#include <algorithm>

namespace jucyaudio
{
    namespace ui
    {
        MarkerRulerComponent::MarkerRulerComponent()
        {
            setSize(600, RULER_HEIGHT);
        }

        void MarkerRulerComponent::paint(juce::Graphics &g)
        {
            const auto bounds = getLocalBounds();

            // Draw ruler background
            g.setColour(juce::Colours::darkgrey.withAlpha(0.3f));
            g.fillRect(bounds);

            // Draw bottom border
            g.setColour(juce::Colours::grey);
            g.drawLine(
                0.0f, static_cast<float>(bounds.getBottom() - 1), static_cast<float>(bounds.getWidth()), static_cast<float>(bounds.getBottom() - 1), 1.0f);

            // Draw time graduations using cached image
            if (m_mixDuration.count() > 0)
            {
                const auto durationMs = m_mixDuration.count();
                const bool needsRebuild = !m_tickCache.isValid() ||
                                          m_tickCacheWidth != bounds.getWidth() ||
                                          m_tickCacheHeight != bounds.getHeight() ||
                                          m_tickCacheDurationMs != durationMs;
                if (needsRebuild)
                {
                    m_tickCacheWidth = bounds.getWidth();
                    m_tickCacheHeight = bounds.getHeight();
                    m_tickCacheDurationMs = durationMs;
                    m_tickCache = juce::Image(juce::Image::ARGB, m_tickCacheWidth, m_tickCacheHeight, true);

                    juce::Graphics cacheG(m_tickCache);
                    const auto pixelsPerMs = static_cast<float>(m_tickCacheWidth) / static_cast<float>(durationMs);

                    // Draw major ticks every 10 seconds
                    cacheG.setColour(juce::Colours::grey.withAlpha(0.5f));
                    cacheG.setFont(10.0f);
                    for (int64_t timeMs = 0; timeMs <= durationMs; timeMs += 10000)
                    {
                        const auto x = static_cast<float>(timeMs) * pixelsPerMs;
                        cacheG.drawLine(x, static_cast<float>(m_tickCacheHeight - 10), x,
                                        static_cast<float>(m_tickCacheHeight), 1.0f);

                        const auto seconds = timeMs / 1000;
                        const auto timeStr = juce::String::formatted("%d:%02d", seconds / 60, seconds % 60);

                        cacheG.setColour(juce::Colours::lightgrey);
                        cacheG.drawText(timeStr, static_cast<int>(x - 20), 0, 40, 12, juce::Justification::centred);
                        cacheG.setColour(juce::Colours::grey.withAlpha(0.5f));
                    }

                    // Draw minor ticks every second
                    cacheG.setColour(juce::Colours::grey.withAlpha(0.3f));
                    for (int64_t timeMs = 0; timeMs <= durationMs; timeMs += 1000)
                    {
                        if (timeMs % 10000 != 0)
                        {
                            const auto x = static_cast<float>(timeMs) * pixelsPerMs;
                            cacheG.drawLine(x, static_cast<float>(m_tickCacheHeight - 5), x,
                                            static_cast<float>(m_tickCacheHeight), 0.5f);
                        }
                    }
                }

                if (m_tickCache.isValid())
                {
                    g.drawImageAt(m_tickCache, 0, 0);
                }
            }
            else if (m_tickCache.isValid())
            {
                m_tickCache = {};
                m_tickCacheWidth = 0;
                m_tickCacheHeight = 0;
                m_tickCacheDurationMs = 0;
            }

            // Draw markers
            for (const auto &marker : m_markers)
            {
                const auto x = getXForTime(marker.position);

                // Determine marker color
                juce::Colour markerColor = juce::Colours::orange;
                if (m_hoveredMarkerId.has_value() && m_hoveredMarkerId.value() == marker.marker_id)
                {
                    markerColor = juce::Colours::yellow;
                }
                else if (marker.color.has_value())
                {
                    // Parse hex color if provided
                    markerColor = juce::Colour::fromString(marker.color.value());
                }

                // Draw marker line
                g.setColour(markerColor);
                g.drawLine(static_cast<float>(x), 0.0f, static_cast<float>(x), static_cast<float>(bounds.getHeight()), 2.0f);

                // Draw marker flag/triangle at top
                juce::Path triangle;
                triangle.addTriangle(static_cast<float>(x - 4), 0.0f, static_cast<float>(x + 4), 0.0f, static_cast<float>(x), 8.0f);
                g.fillPath(triangle);

                // Draw emoji if present
                if (marker.emoji.has_value() && !marker.emoji.value().empty())
                {
                    g.setFont(12.0f);
                    g.drawText(marker.emoji.value(), x - 10, 10, 20, 15, juce::Justification::centred);
                }
            }

            // Draw playback position indicator
            if (m_playbackPosition > 0.0 && m_mixDuration.count() > 0)
            {
                const auto x = static_cast<float>(m_playbackPosition * bounds.getWidth());
                g.setColour(juce::Colours::red);
                g.drawLine(x, 0.0f, x, static_cast<float>(bounds.getHeight()), 1.0f);
            }
        }

        void MarkerRulerComponent::resized()
        {
            // Nothing specific to do on resize
        }

        void MarkerRulerComponent::mouseDown(const juce::MouseEvent &event)
        {
            if (event.mods.isAltDown())
            {
                // Alt+Click: Add new marker
                const auto timeMs = getTimeForX(event.x);
                if (onMarkerAdded)
                {
                    onMarkerAdded(timeMs);
                }
            }
            else
            {
                // Regular click: Check if clicking on existing marker
                const auto marker = findMarkerAtPosition(event.x);
                if (marker.has_value() && onMarkerClicked)
                {
                    onMarkerClicked(marker->marker_id);
                }
            }
        }

        void MarkerRulerComponent::mouseMove(const juce::MouseEvent &event)
        {
            const auto marker = findMarkerAtPosition(event.x);

            if (marker.has_value())
            {
                m_hoveredMarkerId = marker->marker_id;

                // Format time for tooltip
                const auto totalMs = marker->position.count();
                const auto minutes = totalMs / 60000;
                const auto seconds = (totalMs % 60000) / 1000;
                const auto millis = totalMs % 1000;

                m_currentTooltip = juce::String::formatted("%d:%02d.%03d - %s", minutes, seconds, millis, marker->comment.c_str());
            }
            else
            {
                m_hoveredMarkerId.reset();
                m_currentTooltip = "Alt+Click to add marker";
            }

            repaint();
        }

        void MarkerRulerComponent::mouseExit(const juce::MouseEvent &)
        {
            m_hoveredMarkerId.reset();
            m_currentTooltip.clear();
            repaint();
        }

        void MarkerRulerComponent::setMarkers(const std::vector<database::MixMarker> &markers)
        {
            m_markers = markers;
            repaint();
        }

        void MarkerRulerComponent::setMixDuration(std::chrono::milliseconds duration)
        {
            m_mixDuration = duration;
            repaint();
        }

        void MarkerRulerComponent::setPlaybackPosition(double positionMs)
        {
            m_playbackPosition = positionMs / static_cast<double>(m_mixDuration.count());
            repaint();
        }

        std::optional<database::MixMarker> MarkerRulerComponent::findMarkerAtPosition(int xPos) const
        {
            constexpr int CLICK_TOLERANCE = 5; // pixels

            for (const auto &marker : m_markers)
            {
                const auto markerX = getXForTime(marker.position);
                if (std::abs(markerX - xPos) <= CLICK_TOLERANCE)
                {
                    return marker;
                }
            }

            return std::nullopt;
        }

        int MarkerRulerComponent::getXForTime(std::chrono::milliseconds time) const
        {
            if (m_mixDuration.count() == 0)
                return 0;

            const auto ratio = static_cast<double>(time.count()) / static_cast<double>(m_mixDuration.count());
            return static_cast<int>(ratio * getWidth());
        }

        std::chrono::milliseconds MarkerRulerComponent::getTimeForX(int xPos) const
        {
            if (getWidth() == 0)
                return std::chrono::milliseconds{0};

            const auto ratio = static_cast<double>(xPos) / static_cast<double>(getWidth());
            const auto timeMs = static_cast<int64_t>(ratio * m_mixDuration.count());
            return std::chrono::milliseconds{std::max(int64_t{0}, timeMs)};
        }
    } // namespace ui
} // namespace jucyaudio
