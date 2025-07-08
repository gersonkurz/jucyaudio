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
            TimelineComponent(juce::AudioFormatManager &formatManager, juce::AudioThumbnailCache &thumbnailCache);

            // The main method to update the timeline display from the data model.
            void populateFrom(const audio::MixProjectLoader &mixLoader);

            MixTrackComponent *getSelectedTrack() const
            {
                return m_selectedTrack;
            }

            double getCurrentTimePosition() const
            {
                return m_currentTimePosition;
            }

            double getPixelsPerSecond() const
            {
                return m_pixelsPerSecond;
            }

            void setSelectedTrack(MixTrackComponent *track);
            void setCurrentTimePosition(double timeInSeconds);
            void playFromPosition(double timePosition);
            void playSelectedTrackFromPosition(double timePosition);
            bool keyPressed(const juce::KeyPress &key) override;
            void deleteSelectedTrack();

            std::function<void(const juce::File &, double)> onPlaybackRequested;
            std::function<void(double)> onSeekRequested;
            std::function<void(TrackId)> onTrackDeleted;


        private:
            void paint(juce::Graphics &g) override;
            void resized() override;

            void mouseWheelMove(const juce::MouseEvent &event, const juce::MouseWheelDetails &wheel) override;
            void mouseDown(const juce::MouseEvent &event) override;
            void recalculateLayout();
            void maintainViewportPosition(double timeAtMouse, int mouseX);
            void paintOverChildren(juce::Graphics &g);

        private:
            // A helper struct to manage UI components and their model data together.
            struct TrackView
            {
                std::unique_ptr<MixTrackComponent> component;
                const database::MixTrack *mixTrackData;
                const database::TrackInfo *trackInfoData;
            };

            juce::AudioFormatManager &m_formatManager;
            juce::AudioThumbnailCache &m_thumbnailCache;
            std::vector<TrackView> m_trackViews;
            int m_calculatedWidth = 0;
            int m_calculatedHeight = 0;
            // For converting time to pixels
            double m_pixelsPerSecond = 1.5;
            static constexpr double MIN_ZOOM = 1.0;    // 1 pixel per second (zoomed out)
            static constexpr double MAX_ZOOM = 100.0;  // 100 pixels per second (zoomed in)
            static constexpr double ZOOM_FACTOR = 1.2; // 20% zoom steps
            MixTrackComponent *m_selectedTrack = nullptr;
            double m_currentTimePosition = 0.0; // in seconds

            // Helper methods
            MixTrackComponent *getTrackAtPosition(juce::Point<int> position);

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineComponent)
        };
    } // namespace ui
} // namespace jucyaudio