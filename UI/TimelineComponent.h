#pragma once

#include "MixTrackComponent.h"
#include <Audio/MixProjectLoader.h> // Need this for its data types

namespace jucyaudio
{
    namespace ui
    {
        class TimelineComponent : public juce::Component
        {
        public:
            TimelineComponent(juce::AudioFormatManager& formatManager, juce::AudioThumbnailCache &thumbnailCache);

            void paint(juce::Graphics &g) override;
            void resized() override;

            // The main method to update the timeline display from the data model.
            void populateFrom(const audio::MixProjectLoader &mixLoader);

        private:
            // A helper struct to manage UI components and their model data together.
            struct TrackView
            {
                std::unique_ptr<MixTrackComponent> component;
                const database::MixTrack *mixTrackData;
                const database::TrackInfo *trackInfoData;
            };

            juce::AudioFormatManager& m_formatManager;
            juce::AudioThumbnailCache &m_thumbnailCache;
            std::vector<TrackView> m_trackViews;

            // For converting time to pixels
            double m_pixelsPerSecond = 10.0;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineComponent)
        };
    } // namespace ui
} // namespace jucyaudio