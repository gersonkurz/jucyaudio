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
            /**
             * @brief Clipboard support for cut/copy/paste operations
             */
            struct ClipboardData
            {
                database::MixTrack mixTrack;
                database::TrackInfo trackInfo;
                bool isValid = false;
            };
            
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
            bool populateFrom(audio::MixProjectLoader* mixLoader = nullptr);

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
            
            double getPixelsPerSecond() const { return m_pixelsPerSecond; }
            
            void setCurrentTimePosition(double timeInSeconds);
            void playFromPosition(double timePosition);
            void playSelectedTrackFromPosition(double timePosition);
            void playMixFromPosition(double timePosition);

            std::function<void(const juce::File &, double)> onPlaybackRequested;
            std::function<void(double)> onSeekRequested;
            std::function<void(TrackId, std::chrono::milliseconds)> onTrackPositionChanged;
            std::function<void()> onMixChanged;
            std::function<void()> onMixSummaryChanged; // track count / duration changed (add/remove) - refresh node summary + status bar
            std::function<void(double)> onMixPlaybackRequested;
            std::function<void(double)> onMixPlaybackAlwaysRequested; // For double-clicks
            std::function<void()> onMixPlaybackReloadRequested; // For hot-reloading mix data during playback
            std::function<void(int orderInMix, const database::MixTrack&)> onCueAttachChanged;
            std::function<void(int orderInMix, const std::vector<database::EnvelopePoint>&)> onEnvelopeChanged;
            std::function<void(int orderInMix, float newGain)> onGainAdjustmentChanged;
            std::function<void(TrackId)> onShowTrackInLibraryRequested;
            std::function<void(TrackId)> onShowTrackDetailsRequested;
            std::function<void(TrackId)> onSelectedTrackChanged; // drives the genre cloud's context album
            std::function<void(TrackId)> onPlayingTrackChanged;  // fires when the playhead enters a different track
            std::function<void()> onZoomChanged;


            /**
             * @brief Sets the currently selected track, handling the repainting of components.
             *
             * This function manages the m_selectedTrack state. To ensure visual correctness,
             * it repaints the previously selected track (to remove its highlight) before
             * setting the new one, and then repaints the newly selected track (to add its
             * highlight).
             *
             * @param track A pointer to the MixTrackComponent to be selected, or nullptr to clear the selection.
             */
            void setSelectedTrack(MixTrackComponent *track);

            /// @brief Drops any in-progress reorder drag.
            ///
            /// m_draggedTrackForReorder is a bare pointer to a MixTrackComponent that populateFrom
            /// and releaseMixLoader both destroy. Anything that abandons a drag, or is about to
            /// tear the views down, has to come through here first - otherwise the next paint
            /// dereferences it, and the drop indicator stays on screen besides.
            void cancelReorderDrag();

            /// @brief Locks the timeline if its loader no longer describes the database.
            ///
            /// Call after every reload attempt. A failed reload leaves the previous tracks in place -
            /// deliberately, so nothing is left pointing at freed memory - but they are now a picture
            /// of a mix that has moved on. Editing that picture writes positions taken from it onto
            /// rows that are somewhere else: dragging the third row on screen reorders whatever the
            /// third row is now.
            ///
            /// @return True if the timeline is still usable.
            bool lockIfUnloaded();

            /**
             * @brief Stops showing a mix: drops the loader reference and everything built from it.
             *
             * Called when the timeline no longer represents anything - unloading, or a reload that
             * failed. The views go with the pointer, because each one holds a raw pointer into the
             * loader's track vector.
             */
            void releaseMixLoader()
            {
                clearTrackContext();
                cancelReorderDrag();

                // The views go too, not just the pointer. Each TrackView holds a raw pointer into the
                // loader's track vector, so leaving them behind means the next paint, layout, drag or
                // copy reads memory the loader has released. Clearing the loader pointer alone stops
                // new lookups and leaves the old ones exactly where they were.
                m_trackViews.clear();
                removeAllChildren();
                m_cachedNumLanes = -1;

                m_mixLoader = nullptr;
            }

            /**
             * @brief Drops both the selected and the playing track and tells listeners the context is gone.
             *
             * Call whenever the timeline stops representing the mix it was showing (unload, repopulate).
             * Deliberately separate from the gap handling in notifyPlayheadTime(): a gap between tracks
             * keeps the last album on screen, but a torn-down mix must not leave one writable.
             *
             * @note Fires onPlayingTrackChanged unconditionally. setSelectedTrack() alone is not enough -
             *       it short-circuits when nothing was selected, which is exactly the case where the
             *       context came from playback rather than from a click.
             */
            void clearTrackContext();
            
            /**
             * @brief Notifies the timeline that the viewport has resized.
             * 
             * This should be called by the parent component when the viewport size changes
             * to ensure the timeline adjusts its height to use all available space.
             */
            void viewportResized();

            /**
             * @brief Reports the playhead position so the timeline can tell listeners which track is sounding.
             *
             * Called on every playback tick. onPlayingTrackChanged only fires when the containing track
             * actually changes, so this is cheap to call at frame rate.
             *
             * @param timeInSeconds Playhead position on the mix timeline.
             */
            void notifyPlayheadTime(double timeInSeconds);

            /**
             * @brief Recalculates the timeline's total width and repositions all child components.
             *
             * This function should be called whenever a change occurs that could affect the
             * overall size or layout of the timeline, such as a change in zoom level or
             * the addition/removal of tracks. It iterates through all managed tracks to find
             * the maximum end time, calculates the required width to display the entire mix,
             * sets the component's size, and then triggers a call to resized() to update
             * the layout of all child components.
             */
            void refreshLayout();

            /**
             * @brief Repositions a specific track after its cue points have changed.
             *
             * This function recalculates the position of a single track component when its
             * cueStart changes, which affects its position on the timeline. It updates the
             * componentStartTime and then calls resized() to reposition the component.
             *
             * @param trackId The ID of the track to reposition.
             */
            void repositionTrack(TrackId trackId);

            /**
             * @brief Deletes the currently selected track and triggers a full UI refresh.
             *
             * This function orchestrates the entire delete-and-refresh process in a self-contained manner:
             * 1.  It identifies the selected track's ID.
             * 2.  It calls the MixManager to remove the track from the database.
             * 3.  It instructs its internal MixProjectLoader to reload its data from the database,
             *     ensuring the in-memory model is synchronized with the change.
             * 4.  It triggers a full repopulation of the timeline UI from the refreshed loader data.
             *
             * @return true if the track was successfully deleted and the UI was refreshed, false otherwise.
             */
            bool deleteSelectedTrack();
            
            // Read-only mode for exported/locked mixes
            void setReadOnly(bool readOnly) { m_isReadOnly = readOnly; }
            bool isReadOnly() const { return m_isReadOnly; }
            
            // Clipboard operations
            void copySelectedTrackToClipboard();
            void cutSelectedTrackToClipboard();
            void pasteFromClipboard(bool insertBefore);
            bool hasClipboardData() const { return m_clipboard.isValid; }
            void removeAllTracksAfterSelected();
            void refreshAfterDeletion(int deletedOrderInMix);
            
            /**
             * @brief Handles mouse down events on the timeline's background area.
             *
             * This function is the primary entry point for direct interaction with the timeline.
             * Its responsibilities are:
             * 1.  To grab keyboard focus so it can respond to key presses (e.g., Delete).
             * 2.  To calculate the time corresponding to the click position.
             * 3.  To set the visual position of the playhead (via setCurrentTimePosition).
             * 4.  To identify and select the track component that was clicked (if any).
             * 5.  To fire callbacks (onSeekRequested, onMixPlaybackAlwaysRequested) to notify
             *     the parent component of the user's intent to seek or play.
             *
             * @param event The mouse event containing the click position and click count.
             */
            void mouseDown(const juce::MouseEvent &event) override;

            /**
             * @brief Handles mouse drag events for track reordering.
             *
             * This function tracks the mouse position during a drag operation and calculates
             * the target drop position for track reordering.
             *
             * @param event The mouse drag event.
             */
            void mouseDrag(const juce::MouseEvent &event) override;

            /**
             * @brief Handles mouse up events to complete track reordering.
             *
             * This function executes the track reorder operation when the mouse button is released
             * after dragging a track to a new position.
             *
             * @param event The mouse up event.
             */
            void mouseUp(const juce::MouseEvent &event) override;

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
             * This function implements a "zoom-to-cursor" behavior. When the user scrolls
             * the mouse wheel, it calculates a new zoom level (pixels-per-second). It then
             * recalculates the entire timeline layout and adjusts the parent viewport's
             * scroll position to ensure that the time point directly under the mouse cursor
             * remains stationary, creating an intuitive zoom experience.
             *
             * @param event The mouse event, used to get cursor position.
             * @param wheel The mouse wheel details, used to determine the direction and amount of scroll.
             */
            void mouseWheelMove(const juce::MouseEvent &event, const juce::MouseWheelDetails &wheel) override;

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

            /**
             * @brief Finds the track component located at a specific pixel position.
             *
             * Iterates through the managed TrackViews and performs a bounds check to see
             * if the given position is within any of the child components.
             *
             * @param position The pixel coordinate to test.
             * @return A pointer to the MixTrackComponent at the given position, or nullptr if no track was found.
             */
            MixTrackComponent *getTrackAtPosition(juce::Point<int> position) const;

            /**
             * @brief Recalculates all track positions without recreating components.
             *
             * This function updates the audioStartTime and componentStartTime for all tracks
             * based on the current mix data, following the Mix Flow algorithm. It's used when
             * the first track's cueStart changes, which affects the global offset.
             */
            void recalculateTrackPositions();

            void deleteTrackAtIndex(size_t trackIndex);
            bool removeTrackFromMixOnly(int orderInMix);

            /**
             * @brief Converts a mouse position to an orderInMix position.
             *
             * This function finds the concrete track component under the cursor and returns
             * its current 0-based orderInMix value.
             *
             * @param position The mouse position in pixels.
             * @return The calculated orderInMix position, or -1 if the position is invalid.
             */
            int pointToOrderInMix(juce::Point<int> position) const;

            /**
             * @brief A helper struct that tightly couples a UI component with its underlying data.
             *
             * This struct is the primary internal data structure for the timeline. It holds the
             * visual component (MixTrackComponent), pointers to the relevant data model objects
             * (MixTrack and TrackInfo), and the crucial calculated start times that determine
             * the component's position on the timeline.
             */
            struct TrackView
            {
                /** @brief The JUCE component that visually represents the track. This owns the component's memory. */
                std::unique_ptr<MixTrackComponent> component;

                /** @brief A non-owning pointer to the mutable mix data (cues, attaches, envelopes). */
                database::MixTrack *mixTrackData{nullptr};

                /** @brief A non-owning pointer to the immutable source audio file data (duration, title, etc.). */
                const database::TrackInfo *trackInfoData{nullptr};

                /** @brief The calculated absolute start time of the track's *audio content* on the mix timeline. */
                Duration_t audioStartTime{0};

                /** @brief The calculated absolute start time of the track's *visual component* on the mix timeline.
                 *         This is equal to `audioStartTime + cueStart`. */
                Duration_t componentStartTime{0};
            };

            /** @brief A non-owning pointer to the MixProjectLoader that serves as the master data source for this timeline. */
            audio::MixProjectLoader *m_mixLoader = nullptr;

            /** @brief A cache of the audio format readers (e.g., for MP3, WAV) required to generate thumbnails. */
            juce::AudioFormatManager &m_formatManager;

            /** @brief A cache that stores generated audio thumbnails to avoid re-reading files from disk. */
            juce::AudioThumbnailCache &m_thumbnailCache;

            /** @brief The primary data store for the timeline, containing all the TrackView instances. */
            std::vector<TrackView> m_trackViews;

            /** @brief The calculated total width in pixels required to display the entire mix at the current zoom level. */
            int m_calculatedWidth = 0;

            /** @brief The calculated total height in pixels for the timeline component. */
            int m_calculatedHeight = 0;
            
            /** @brief Cached number of lanes to avoid unnecessary recalculations during resize */
            int m_cachedNumLanes = -1;

            /** @brief Flag to indicate zoom has changed and track bounds need recalculation */
            bool m_zoomHasChanged = false;

            /** @brief A non-owning pointer to the currently selected MixTrackComponent. */
            MixTrackComponent *m_selectedTrack = nullptr;

            /** @brief Track the playhead was last inside, so onPlayingTrackChanged only fires on a change. */
            TrackId m_playingTrackId{-1};
            
            /** @brief Read-only mode flag for exported/locked mixes */
            bool m_isReadOnly = false;

            /** @brief The current position of the user-controlled playhead (the white line), in seconds. A value of -1.0 indicates it is not set. */
            double m_currentTimePosition = -1.0;

            /** @brief The current position of the actual mix playback engine (the red line), in seconds. A value of -1.0 indicates no playback. */
            double m_mixPlaybackPosition = -1.0;

            // ------ zooming -------
            /**
             * @brief The core scaling factor for the timeline, representing the number of
             * horizontal pixels used to display one second of audio.
             * A higher value means the timeline is more "zoomed in". This value is
             * modified by the mouseWheelMove function.
             */
            // ------ Constants -------
            /** @brief Minimum zoom level (pixels per second) */
            static constexpr double MIN_ZOOM = 0.25;
            
            /** @brief Maximum zoom level (pixels per second) */
            static constexpr double MAX_ZOOM = 400.0;
            
            /** @brief Zoom factor for mouse wheel operations (20% steps) */
            static constexpr double ZOOM_FACTOR = 1.2;
            
            /** @brief Vertical gap between track lanes (pixels) */
            static constexpr int TRACK_LANE_GAP = 5;
            
            /** @brief Height of the time ruler at the top (pixels) */
            static constexpr int RULER_HEIGHT = 30;
            
            /** @brief Default number of lanes for height calculation */
            static constexpr int DEFAULT_NUM_LANES = 8;
            
            /** @brief Extra width added after the last track (pixels) */
            static constexpr int TIMELINE_PADDING = 200;
            
            /** @brief Interval between time markers in seconds */
            static constexpr int TIME_MARKER_INTERVAL = 30;
            
            /** @brief Hit threshold for marker detection (pixels) */
            static constexpr int MARKER_HIT_THRESHOLD = 5;
            
            // ------ Member Variables -------
            /** @brief The core scaling factor for the timeline (pixels per second) */
            double m_pixelsPerSecond = 2.5;

            /** @brief The preview time for cue drag operations, showing where the edge will be if released. */
            std::optional<Duration_t> m_cueDragPreviewTime;
            
            /** @brief Flag to indicate if the timeline is currently being populated, to prevent unwanted save triggers. */
            bool m_isPopulating = false;
            
            /** @brief Clipboard for cut/copy/paste operations */
            ClipboardData m_clipboard;

            // ------ Track Reordering (Drag-and-Drop) State -------
            /** @brief Flag indicating if a track is currently being dragged for reordering */
            bool m_isDraggingTrackForReorder = false;

            /** @brief The track component being dragged for reordering */
            MixTrackComponent* m_draggedTrackForReorder = nullptr;

            /** @brief The target position (orderInMix) where the track will be dropped */
            int m_dropTargetOrderInMix = -1;

            /** @brief The starting mouse position when drag began */
            juce::Point<int> m_trackDragStartPosition;

            /** @brief The current mouse position during drag (for floating rectangle visualization) */
            juce::Point<int> m_currentDragPosition;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineComponent)
        };
    } // namespace ui
} // namespace jucyaudio
