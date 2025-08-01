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

        // Visually represents a single track segment on the timeline.
        class MixTrackComponent : public juce::Component, public juce::ChangeListener
        {
        public:
            // We can define the layout constants publicly for the TimelineComponent to use
            static constexpr int textSectionHeight = 20;
            static constexpr int waveformSectionHeight = 120; // Doubled from 60 to 120
            static constexpr int totalHeight = textSectionHeight + waveformSectionHeight;

            MixTrackComponent(MixTrack &mixTrack,
                const TrackInfo &trackInfo,
                juce::AudioFormatManager &formatManager,
                juce::AudioThumbnailCache &thumbnailCache);

            ~MixTrackComponent() override;

            std::function<void(TrackId, const std::vector<EnvelopePoint> &)> onEnvelopeChanged;
            std::function<void(TrackId, const MixTrack &)> onCueAttachChanged;

        private:
            enum class MarkerType
            {
                None,
                CueStart,
                CueEnd,
                AttachFrom,
                AttachTo
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

            // Marker-related helper methods
            MarkerType hitTestMarker(juce::Point<int> mousePos) const;

            /**
             * @brief Converts a logical marker type into a horizontal pixel coordinate within the component.
             *
             * This is a critical helper function that translates an abstract time value from the
             * data model into a concrete on-screen position. It now only handles Attach markers.
             *
             * The conversion algorithm is as follows:
             * 1.  It resolves the MarkerType to its corresponding absolute time value (markerTime) from the MixTrack data.
             * 2.  It calculates the marker's time relative to the component's visual start (relativeTime = markerTime - cueStart).
             *     This correctly positions the marker within the component's own timeline, where cueStart is time zero.
             * 3.  It calculates the proportion of this relativeTime to the component's total effectiveDuration.
             * 4.  This proportion is then applied to the component's pixel width to get the final X coordinate.
             *
             * @param marker The type of marker to find the position for (AttachFrom or AttachTo).
             * @return The integer X coordinate for the marker, relative to the component's top-left corner.
             */
            int getMarkerXPosition(MarkerType marker) const;

            Duration_t screenXToTrackTime(int screenX) const;
            void updateMarkerPosition(MarkerType marker, int newX);

            void mouseDown(const juce::MouseEvent &event) override;
            void mouseDrag(const juce::MouseEvent &event) override;
            void mouseUp(const juce::MouseEvent &event) override;

            /**
             * @brief Handle mouse-move event to update the position of attach markers.
             *
             * If you hover over an enevelope point and/or marker , highlight it visually to indicate you can drag it,
             *
             * @param event Position on the screen
             */
            void mouseMove(const juce::MouseEvent &event) override;

            Duration_t xToTime(int x) const;

            MixTrack &m_mixTrack;
            const TrackInfo &m_trackInfo;
            juce::AudioThumbnail m_thumbnail;
            juce::Label m_infoLabel;

            enum class EnvelopePointState
            {
                Normal,
                Hovered,
                Selected
            };

            std::optional<size_t> m_selectedEnvelopePointIndex;
            std::optional<size_t> m_hoveredEnvelopePointIndex;
            bool m_isDraggingEnvelopePoint = false;
            juce::Point<int> m_envelopePointDragStart;
            EnvelopePoint m_originalEnvelopePoint;

            // For cue/attach marker dragging
            MarkerType m_draggedMarker = MarkerType::None;
            MarkerType m_hoveredMarker = MarkerType::None;
            MixTrack m_originalMixTrack;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixTrackComponent)
        };
    } // namespace ui
} // namespace jucyaudio
