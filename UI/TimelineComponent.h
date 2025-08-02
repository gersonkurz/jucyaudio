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

            /**
             * @brief Populates the timeline with track components based on a mix data model.
             *
             * This is the primary function for building or refreshing the timeline display. It
             * is a direct implementation of our "Mix Flow" algorithm:
             *
             * 1.  **Anchor:** It first calculates a 'globalOffset' if the very first track in the
             *     mix has a negative cueStart. This ensures the entire mix is shifted to the right
             *     so that no component is ever drawn at a negative x-coordinate.
             *
             * 2.  **Iterative Placement:** It then iterates through each track in the mix,
             *     calculating the precise start time for each track's audio content using the
             *     formula:
             *     `AudioStartTime(N) = AudioStartTime(N-1) + Track(N-1).attachTo - Track(N).attachFrom`
             *
             * 3.  **Component Creation:** For each track, it creates a MixTrackComponent, storing
             *     the calculated start times and wiring up the necessary callbacks so the parent
             *     timeline can listen for changes from its children.
             *
             * @param mixLoader A reference to the MixProjectLoader that holds the mix data.
             */
            void populateFrom(audio::MixProjectLoader &mixLoader);

            MixTrackComponent *getSelectedTrack() const
            {
                return m_selectedTrack;
            }

            double getCurrentTimePosition() const
            {
                return m_currentTimePosition;
            }
            
            double getMixPlaybackPosition() const
            {
                return m_mixPlaybackPosition;
            }
            
            void setMixPlaybackPosition(double position)
            {
                m_mixPlaybackPosition = position;
                repaint();
            }

            double getPixelsPerSecond() const
            {
                return m_pixelsPerSecond;
            }

            void setSelectedTrack(MixTrackComponent *track);
            void setCurrentTimePosition(double timeInSeconds);
            void playFromPosition(double timePosition);
            void playSelectedTrackFromPosition(double timePosition);
            void playMixFromPosition(double timePosition);
            void refreshLayout(); // Refresh timeline layout without recreating components

            std::function<void(const juce::File &, double)> onPlaybackRequested;
            std::function<void(double)> onSeekRequested;
            std::function<void(TrackId)> onTrackDeleted;
            std::function<void(TrackId, std::chrono::milliseconds)> onTrackPositionChanged;
            std::function<void()> onMixChanged;
            std::function<void(double)> onMixPlaybackRequested;
            std::function<void(double)> onMixPlaybackAlwaysRequested; // For double-clicks
            std::function<void(TrackId, const database::MixTrack&)> onCueAttachChanged;
            std::function<void(TrackId, const std::vector<database::EnvelopePoint>&)> onEnvelopeChanged;

        private:
            /**
             * @brief Handles key press events when the timeline has focus.
             *
             * This function provides keyboard shortcuts for common timeline actions.
             * - Spacebar: Toggles playback of the mix from the current playhead position.
             * - Escape: Stops mix playback.
             * - Delete/Backspace: Deletes the currently selected track component.
             *
             * @param key The key press event to be handled.
             * @return true if the key press was consumed, false otherwise.
             */
            bool keyPressed(const juce::KeyPress &key) override;
            
            /**
             * @brief Deletes the currently selected track from the timeline and data model.
             *
             * This function is triggered by the keyPressed handler. It identifies the selected
             * track, removes its UI component, notifies the parent via the onTrackDeleted
             * callback to update the master data model, and then attempts to trigger a
             * visual refresh.
             */
            void deleteSelectedTrack();
            
            /**
             * @brief Draws the background and the time grid of the timeline.
             */
            void paint(juce::Graphics &g) override;

            /**
             * @brief Sets the size and position of all child MixTrackComponent instances.
             *
             * This function is called whenever the timeline's size changes. It is responsible
             * for the visual layout. It iterates through all the managed TrackViews and sets
             * the bounds of each child component based on the time values that were pre-calculated
             * in populateFrom().
             *
             * - The component's X position is determined by its `componentStartTime`.
             * - The component's width is determined by its `getEffectiveDuration`.
             * - The Y position is calculated using a simple lane-switching algorithm for visual clarity.
             */
            void resized() override;

            /**
             * @brief Handles mouse wheel events to control the timeline's zoom level.
             *
             * This function implements a "zoom-to-cursor" behavior. When the user holds the
             * Control key and scrolls the mouse wheel, it calculates a new zoom level
             * (pixels-per-second). It then recalculates the entire timeline layout and adjusts
             * the parent viewport's scroll position to ensure that the time point directly
             * under the mouse cursor remains stationary, creating an intuitive zoom experience.
             *
             * @param event The mouse event, used to check for modifier keys and get cursor position.
             * @param wheel The mouse wheel details, used to determine the direction and amount of scroll.
             */
            void mouseWheelMove(const juce::MouseEvent &event, const juce::MouseWheelDetails &wheel) override;

            void mouseDown(const juce::MouseEvent &event) override;
            
            /**
             * @brief Adjusts the parent viewport's scroll position to keep a specific time point stable under the mouse cursor during a zoom operation.
             *
             * This function is the core of the "zoom-to-cursor" feature. Without it, zooming
             * would always appear to be anchored to the top-left of the component. This
             * method calculates how far the timeline content needs to be shifted horizontally
             * so that the time point that was under the mouse before the zoom is still under
             * the mouse after the zoom.
             *
             * @param timeAtMouse The abstract time (in seconds) that was under the cursor *before* the zoom.
             * @param mouseX The horizontal pixel position of the cursor *before* the zoom.
             */
            void maintainViewportPosition(double timeAtMouse, int mouseX);
            
            /**
             * @brief Draws overlays on top of the child components, like the playheads.
             */
            void paintOverChildren(juce::Graphics &g) override;

            /**
             * @brief Renders the vertical lines that indicate where tracks are attached.
             *
             * This function correctly calculates the absolute on-screen position for each
             * "attachTo" point by adding its time value to the track's calculated 'audioStartTime'.
             * This ensures the line is drawn at the correct global time, visually connecting tracks.
             */
            void drawCrossfadeLines(juce::Graphics &g);

            // A helper struct to manage UI components and their model data together.
            struct TrackView
            {
                std::unique_ptr<MixTrackComponent> component;

                // this is NOT const, because it IS going to be modified (e.g. envelope points)
                database::MixTrack *mixTrackData;
                const database::TrackInfo *trackInfoData;
                Duration_t audioStartTime{0};     // The start time of the audio content, from the ATTACH formula.
                Duration_t componentStartTime{0}; // The visual start time of the component on the timeline.
            };

            MixTrackComponent *m_draggingTrack = nullptr;
            juce::AudioFormatManager &m_formatManager;
            juce::AudioThumbnailCache &m_thumbnailCache;
            std::vector<TrackView> m_trackViews;
            int m_calculatedWidth = 0;
            int m_calculatedHeight = 0;

            MixTrackComponent *m_selectedTrack = nullptr;
            double m_currentTimePosition = -1.0; // in seconds (for click position, -1 means not set)
            double m_mixPlaybackPosition = -1.0; // in seconds (for mix playback, -1 means not playing)

            // Helper methods
            MixTrackComponent *getTrackAtPosition(juce::Point<int> position);
            TrackId getTrackIdForComponent(MixTrackComponent *component);
            void updateTrackPosition(TrackId trackId, double newTimeInSeconds);

            
            // ------ zooming -------
            /**
             * @brief The core scaling factor for the timeline, representing the number of
             * horizontal pixels used to display one second of audio.
             * A higher value means the timeline is more "zoomed in". This value is
             * modified by the mouseWheelMove function.
             */
            double m_pixelsPerSecond = 2.5;             // A reasonable default zoom level.
            static constexpr double MIN_ZOOM = 1.0;     // 1 pixel per second (zoomed out)
            static constexpr double MAX_ZOOM = 100.0;   // 100 pixels per second (zoomed in)
            static constexpr double ZOOM_FACTOR = 1.2;  // 20% zoom steps
            
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineComponent)
        };
    } // namespace ui
} // namespace jucyaudio