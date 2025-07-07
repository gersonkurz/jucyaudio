#include <UI/TimelineComponent.h>
#include <spdlog/spdlog.h>
#include <toml++/toml.h> // Include the parser implementation here

namespace jucyaudio
{
    namespace ui
    {
        TimelineComponent::TimelineComponent(juce::AudioFormatManager& formatManager, juce::AudioThumbnailCache &thumbnailCache)
            : m_formatManager{formatManager},
              m_thumbnailCache{thumbnailCache}
        {
        }

        void TimelineComponent::paint(juce::Graphics &g)
        {
            // Draw the main background
            g.fillAll(getLookAndFeel().findColour(juce::TreeView::backgroundColourId));

            // Draw the time grid
            g.setColour(getLookAndFeel().findColour(juce::TextEditor::outlineColourId));

            const int numMarkers = static_cast<int>(getWidth() / (30 * m_pixelsPerSecond));
            for (int i = 0; i <= numMarkers; ++i)
            {
                const float x = static_cast<float>(i * 30 * m_pixelsPerSecond);
                g.drawVerticalLine(juce::roundToInt(x), 0.0f, static_cast<float>(getHeight()));

                int minutes = (i * 30) / 60;
                int seconds = (i * 30) % 60;
                juce::String time = juce::String::formatted("%d:%02d", minutes, seconds);
                g.drawText(time, juce::roundToInt(x) + 4, 4, 100, 20, juce::Justification::topLeft);
            }
        }

        void TimelineComponent::resized()
        {
            // When the timeline resizes, we need to reposition all our track components.
            const int trackHeight = 80;
            int yPos = 30; // Start below the time markers

            for (const auto &view : m_trackViews)
            {
                const auto startTime = std::chrono::duration<double>(view.mixTrackData->mixStartTime).count();
                const auto duration = std::chrono::duration<double>(view.trackInfoData->duration).count();

                const int x = static_cast<int>(startTime * m_pixelsPerSecond);
                const int width = static_cast<int>(duration * m_pixelsPerSecond);

                view.component->setBounds(x, yPos, width, trackHeight);

                yPos += trackHeight + 10; // Stack tracks vertically
            }
        }

        void TimelineComponent::populateFrom(const audio::MixProjectLoader &mixLoader)
        {
            // Clear all existing components
            m_trackViews.clear();
            removeAllChildren();

            for (const auto &mixTrack : mixLoader.getMixTracks())
            {
                const auto *trackInfo = mixLoader.getTrackInfoForId(mixTrack.trackId);
                if (trackInfo)
                {
                    TrackView view;
                    view.mixTrackData = &mixTrack;
                    view.trackInfoData = trackInfo;
                    view.component = std::make_unique<MixTrackComponent>(*view.mixTrackData, *view.trackInfoData, m_formatManager, m_thumbnailCache);
                    addAndMakeVisible(*view.component);
                    m_trackViews.push_back(std::move(view));
                }
            }

            // Trigger a call to resized() to position the new components.
            resized();
        }
    } // namespace ui
} // namespace jucyaudio