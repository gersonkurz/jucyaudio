#pragma once

#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <juce_audio_utils/juce_audio_utils.h> // For AudioThumbnail
#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
        using namespace database;

        /**
         * @brief Visual representation of a single track segment on the timeline.
         *
         * This component displays:
         * - Track title and position information (top section)
         * - Audio waveform with silence regions (bottom section)
         * - Volume envelope with draggable control points
         * - Attach markers for track synchronization
         * - Interactive edges for adjusting cue points
         *
         * The component supports dragging of:
         * - Cue points (edges) to adjust track segment boundaries
         * - Attach points to control track synchronization
         * - Envelope points to adjust volume over time
         *
         * Inherits from ChangeListener to receive waveform thumbnail updates.
         */
        class MixTrackComponent : public juce::Component, public juce::ChangeListener
        {            
            /** @brief Thickness of the path between envelope points */
            static constexpr double ENVELOPE_PATH_LINE_THICKESS = 4.0f;

            /** @brief Radius of an envelope point not active */
            static constexpr double ENVELOPE_POINT_STANDARD_RADIUS = 10.0f;

            /** @brief Radius of an envelope point that is currently being dragged / hovered upon */
            static constexpr double ENVELOPE_POINT_ACTIVE_RADIUS = 20.0f;

            /** @brief Height of the text section displaying track title (pixels) */
            static constexpr int TEXT_SECTION_HEIGHT = 20;

            /** @brief Height of the waveform display section (pixels) */
            static constexpr int WAVEFORM_SECTION_HEIGHT = 120;

        public:
            
            /** @brief Total height of the component (text + waveform) */
            static constexpr int TOTAL_COMPONENT_HEIGHT = TEXT_SECTION_HEIGHT + WAVEFORM_SECTION_HEIGHT;

            MixTrackComponent(MixTrack &mixTrack,
                const TrackInfo &trackInfo,
                juce::AudioFormatManager &formatManager,
                juce::AudioThumbnailCache &thumbnailCache);

            ~MixTrackComponent() override;

            // Callback when envelope points change
            std::function<void(TrackId, const std::vector<EnvelopePoint> &)> onEnvelopeChanged;
            
            // Callback when cue or attach points change
            std::function<void(TrackId, const MixTrack &)> onCueAttachChanged;
            
            // Callback during cue/attach point dragging for visual feedback
            // isAttachPoint: true for attach points, false for cue points
            // previewTime: the time position being previewed, or nullopt to clear
            std::function<void(TrackId, bool isAttachPoint, std::optional<Duration_t>)> onCueDragInProgress;

            /**
             * @brief Returns the track ID of this mix track.
             *
             * @return The ID of this track.
             */
            const auto getTrackId() const
            {
                return m_mixTrack.trackId;
            }

        private:
            /**
             * @brief Enumeration of interactive marker types in the component.
             */
            enum class MarkerType
            {
                None,        ///< No marker (default state)
                CueStart,    ///< Left edge of component (start of track segment)
                CueEnd,      ///< Right edge of component (end of track segment)
                AttachFrom,  ///< Sync point in this track that aligns with previous track
                AttachTo     ///< Sync point in this track where next track will align
            };

            /**
             * @brief Renders the visual representation of the track segment.
             *
             * This method is the core of the visual system and operates on a "three-part" model.
             * It does not change the component's size or position; it only draws within its given bounds.
             *
             * The logic is as follows:
             * 1.  It calculates the proportional duration of three distinct regions:
             *     - [silence-before]: The silence added by a negative cueStart.
             *     - [waveform-content]: The audible portion of the source audio file.
             *     - [silence-after]: The silence added by a positive cueEnd.
             * 2.  It uses these proportions to calculate specific sub-rectangles within the component's bounds for each region.
             * 3.  It determines which time-slice of the source audio file to use.
             * 4.  It draws the correct audio thumbnail slice into the calculated [waveform-content] rectangle.
             * 5.  Finally, it draws overlays (like markers and envelopes) in the correct coordinate spaces.
             */
            void paint(juce::Graphics &g) override;

            /**
             * @brief Called by the JUCE framework whenever the component's size or position changes.
             *
             * This function is responsible for setting the position and size of any child components.
             * In this case, it positions the m_infoLabel at the top of the component, filling the
             * text section, with a small horizontal margin.
             */
            void resized() override;

            /**
             * @brief Receives callbacks from ChangeBroadcaster objects this component is listening to.
             *
             * This component listens for changes to its AudioThumbnail. The thumbnail generation
             * happens on a background thread. When the thumbnail has new data to display, it sends
             * a change message. This callback receives that message and triggers a repaint() of the
             * component to draw the newly available waveform data.
             *
             * @param source A pointer to the ChangeBroadcaster that sent the notification.
             */
            void changeListenerCallback(juce::ChangeBroadcaster *source) override;

            /**
             * @brief Checks if this component is currently the selected track in the parent timeline.
             *
             * The selection state is managed by the parent TimelineComponent. This method provides
             * a clean way to query that state, which is used by the paint() method to determine
             * whether to draw a selection highlight.
             *
             * @return true if this component is the currently selected one, false otherwise.
             */
            bool isSelected() const;

            /**
             * @brief Draws the volume envelope line and its interactive points.
             *
             * This function renders the envelope across the component's ENTIRE effective duration,
             * including any silence regions added by cue points. It is therefore crucial that the
             * 'area' parameter passed to this function is the full waveform area from the parent
             * component, not just the sub-rectangle containing the visible audio.
             *
             * @param g The graphics context to draw into.
             * @param area The rectangle representing the component's full waveform area.
             */
            void drawVolumeEnvelope(juce::Graphics &g, const juce::Rectangle<int> &envelopeArea);

            /**
             * @brief Renders the two attach markers (AttachFrom, AttachTo) for this track.
             *
             * This function's sole responsibility is to render the visual representation of
             * the attach points, which define how tracks link together. It relies entirely
             * on the getMarkerXPosition() helper to determine the correct horizontal position.
             * It does NOT draw cue start/end markers, as those are represented by the
             * component's visible edges.
             *
             * @param g The graphics context to draw into.
             * @param area The rectangle representing the component's full waveform area.
             */
            void drawAttachMarkers(juce::Graphics &g, juce::Rectangle<int> area);

            /**
             * @brief Checks if the mouse cursor is currently over an envelope point.
             *
             * This function iterates through all envelope points, converts their logical position
             * to a screen coordinate using the robust envelopePointToScreenPosition() helper,
             * and checks if the mouse position is within a small radius of that coordinate.
             *
             * @param mousePos The current mouse position to test.
             * @return An optional containing the index of the hit point, or nullopt if no point was hit.
             */
            std::optional<size_t> hitTestEnvelopePoint(juce::Point<int> mousePos) const;

            /**
             * @brief Converts an abstract envelope point (time/volume) to a PRECISE screen coordinate.
             *
             * This is the canonical function for converting envelope point data into a visual
             * position. It returns a juce::Point<float> for sub-pixel accuracy, which is
             * required by drawing primitives like juce::Path.
             *
             * @param point The envelope point from the data model.
             * @return The corresponding (x, y) floating-point coordinate on the component.
             */
            juce::Point<float> envelopePointToScreenPosition(const EnvelopePoint &point) const;

            /**
             * @brief Converts a screen pixel coordinate back into a logical EnvelopePoint.
             *
             * This is the inverse of envelopePointToScreenPosition. It takes a mouse position
             * and correctly maps it back to a time and volume value based on the component's
             * full effective duration, including any silence regions. This allows envelope
             * points to be dragged into and exist in those silence regions.
             *
             * @param screenPos The mouse position on the component.
             * @return An EnvelopePoint struct with the corresponding time and volume.
             */
            EnvelopePoint screenPositionToEnvelopePoint(juce::Point<int> screenPos) const;

            /**
             * @brief Constrains an envelope point's position to valid bounds.
             *
             * This function ensures that an envelope point respects two sets of rules:
             * 1.  It cannot move past its immediate neighbors, preserving the chronological order of points.
             * 2.  It cannot be dragged outside the component's total effective duration, which includes
             *     any silence regions defined by cueStart and cueEnd.
             *
             * @param pointIndex The index of the point being constrained.
             * @param point The envelope point to be modified.
             */
            void constrainEnvelopePoint(size_t pointIndex, EnvelopePoint &point) const;

            /**
             * @brief Tests if the mouse position is over a draggable marker.
             *
             * This function checks if the mouse is within the hit threshold (5 pixels) of any
             * marker line (CueStart, CueEnd, AttachFrom, AttachTo). It only tests within the
             * waveform area of the component.
             *
             * @param mousePos The mouse position to test, in component coordinates.
             * @return The type of marker under the mouse, or MarkerType::None if no hit.
             */
            MarkerType hitTestMarker(juce::Point<int> mousePos) const;

            /**
             * @brief Converts a logical marker type into a horizontal pixel coordinate within the component.
             *
             * This is a critical helper function that translates an abstract time value from the
             * data model into a concrete on-screen position. It handles all marker types: CueStart,
             * CueEnd, AttachFrom, and AttachTo.
             *
             * The conversion algorithm is as follows:
             * 1.  It resolves the MarkerType to its corresponding absolute time value (markerTime) from the MixTrack data.
             * 2.  It calculates the marker's time relative to the component's visual start (relativeTime = markerTime - cueStart).
             *     This correctly positions the marker within the component's own timeline, where cueStart is time zero.
             * 3.  It calculates the proportion of this relativeTime to the component's total effectiveDuration.
             * 4.  This proportion is then applied to the component's pixel width to get the final X coordinate.
             *
             * @param marker The type of marker to find the position for.
             * @return The integer X coordinate for the marker, relative to the component's top-left corner.
             */
            int getMarkerXPosition(MarkerType marker) const;

            /**
             * @brief Handles the initial mouse down event on the component.
             *
             * This function determines which, if any, interactive element was clicked and
             * sets the initial state for a potential drag operation. It uses a
             * "first-come, first-served" priority: envelope points are checked before
             * markers, and a general track selection happens only if no specific
             * interactive element was hit.
             *
             * @param event The mouse event containing the click position and button state.
             */
            void mouseDown(const juce::MouseEvent &event) override;
            
            /**
             * @brief Handles mouse drag events for all draggable elements.
             *
             * This function is called continuously after a mouseDown event as long as the
             * left mouse button is held down and the mouse is moving. It handles:
             * - Cue marker dragging (CueStart/CueEnd) with preview feedback
             * - Attach marker dragging (AttachFrom/AttachTo) with constraints and preview
             * - Envelope point dragging with real-time updates
             *
             * @param event The mouse event containing the current cursor position.
             */
            void mouseDrag(const juce::MouseEvent &event) override;

            /**
             * @brief Handles the mouse button release event, finalizing any drag operations.
             *
             * This function is called when a drag is completed. It:
             * - Updates the final position of dragged markers (cue/attach points)
             * - Applies constraints to ensure valid positions
             * - Notifies the parent TimelineComponent via callbacks
             * - Clears visual feedback and resets drag state
             *
             * @param event The mouse event containing the release position.
             */
            void mouseUp(const juce::MouseEvent &event) override;

            /**
             * @brief Handles mouse movement to provide visual feedback for draggable elements.
             * 
             * This function is called when the mouse moves over the component. It:
             * - Detects hovering over envelope points and markers
             * - Updates the mouse cursor to indicate draggability
             * - Changes cursor to resize arrows for cue edges, pointing hand for other elements
             *
             * @param event The mouse event containing the current position
             */
            void mouseMove(const juce::MouseEvent &event) override;

            /**
             * @brief Handles key press events, specifically ESC to cancel drag operations.
             *
             * When ESC is pressed during a drag operation:
             * - Cancels the current drag (marker or envelope point)
             * - Restores original envelope point position if applicable
             * - Clears visual feedback
             *
             * @param key The key that was pressed.
             * @return true if the key was handled, false otherwise.
             */
            bool keyPressed(const juce::KeyPress &key) override;

            /**
             * @brief Converts a horizontal pixel coordinate into a logical time value.
             * @param x The horizontal pixel position relative to the component's left edge.
             * @param clampToComponentBounds If true, the time will be clamped to the component's
             *                               visual start/end. If false, it can report times
             *                               outside the component's bounds (e.g., when dragging).
             * @return The calculated time as a Duration_t.
             */
            Duration_t xToTime(int x, bool clampToComponentBounds) const;

            /** @brief Reference to the mix track data (cue points, attach points, envelopes) */
            MixTrack &m_mixTrack;
            
            /** @brief Reference to the track information (duration, title, file path) */
            const TrackInfo &m_trackInfo;
            
            /** @brief JUCE component for generating and caching the audio waveform visualization */
            juce::AudioThumbnail m_thumbnail;
            
            /** @brief Label displaying track title and mix position in the top section */
            juce::Label m_infoLabel;
            
            /** @brief Index of the currently selected envelope point, if any */
            std::optional<size_t> m_selectedEnvelopePointIndex;
            
            /** @brief Index of the envelope point currently under the mouse cursor */
            std::optional<size_t> m_hoveredEnvelopePointIndex;
            
            /** @brief Flag indicating an envelope point drag operation is in progress */
            bool m_isDraggingEnvelopePoint = false;
            
            /** @brief Starting mouse position when envelope point dragging began */
            juce::Point<int> m_envelopePointDragStart;
            
            /** @brief Original envelope point data before dragging (for ESC cancellation) */
            EnvelopePoint m_originalEnvelopePoint;

            /** @brief Type of marker currently being dragged (None if no drag in progress) */
            MarkerType m_draggedMarker = MarkerType::None;
            
            /** @brief Type of marker currently under the mouse cursor (for hover feedback) */
            MarkerType m_hoveredMarker = MarkerType::None;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixTrackComponent)
        };
    } // namespace ui
} // namespace jucyaudio
