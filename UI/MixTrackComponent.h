#pragma once

#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <juce_audio_utils/juce_audio_utils.h> // For AudioThumbnail
#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
        // Visually represents a single track segment on the timeline.
        class MixTrackComponent : public juce::Component, public juce::ChangeListener
        {
        public:
            // We can define the layout constants publicly for the TimelineComponent to use
            static constexpr int textSectionHeight = 20;
            static constexpr int waveformSectionHeight = 120;  // Doubled from 60 to 120
            static constexpr int totalHeight = textSectionHeight + waveformSectionHeight;

            MixTrackComponent(const database::MixTrack &mixTrack, const database::TrackInfo &trackInfo, juce::AudioFormatManager &formatManager,
                              juce::AudioThumbnailCache &thumbnailCache);

            ~MixTrackComponent() override;

            std::function<void(TrackId, const std::vector<database::EnvelopePoint>&)> onEnvelopeChanged;
            std::function<void(TrackId, const database::MixTrack&)> onCueAttachChanged;

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
            void resized() override; // We'll add this to position our label

            void changeListenerCallback(juce::ChangeBroadcaster *source) override;
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
            void drawCueAndAttachMarkers(juce::Graphics &g, juce::Rectangle<int> area);
            
            std::optional<size_t> hitTestEnvelopePoint(juce::Point<int> mousePos) const;
            juce::Point<int> envelopePointToScreenPosition(const database::EnvelopePoint &point) const;
            database::EnvelopePoint screenPositionToEnvelopePoint(juce::Point<int> screenPos) const;
            void constrainEnvelopePoint(size_t pointIndex, database::EnvelopePoint &point) const;
            
            // Marker-related helper methods
            MarkerType hitTestMarker(juce::Point<int> mousePos) const;
            int getMarkerXPosition(MarkerType marker) const;
            Duration_t screenXToTrackTime(int screenX) const;
            void updateMarkerPosition(MarkerType marker, int newX);
            
            void mouseDown(const juce::MouseEvent &event) override;
            void mouseDrag(const juce::MouseEvent &event) override;
            void mouseUp(const juce::MouseEvent &event) override;
            void mouseMove(const juce::MouseEvent &event) override;
            void setTopLeftPositionWithLogging(int newX, int newY);
            Duration_t xToTime(int x) const;

            // Custom constrainer that only allows horizontal movement
            class HorizontalOnlyConstrainer : public juce::ComponentBoundsConstrainer
            {
            public:
                void setLockedY(int y)
                {
                    m_lockedY = y;
                }

                void checkBounds(juce::Rectangle<int> &bounds, const juce::Rectangle<int> & /*previousBounds*/, const juce::Rectangle<int> & /*limits*/,
                                 bool /*isStretchingTop*/, bool /*isStretchingLeft*/, bool /*isStretchingBottom*/, bool /*isStretchingRight*/) override
                {
                    // Lock the Y position - only allow horizontal movement
                    bounds.setY(m_lockedY);

                    // Prevent dragging before X=0 (before timeline start)
                    if (bounds.getX() < 0)
                        bounds.setX(0);
                }

            private:
                int m_lockedY = 0;
            };

            const database::MixTrack &m_mixTrack;
            const database::TrackInfo &m_trackInfo;
            juce::AudioThumbnail m_thumbnail;
            juce::Label m_infoLabel;

            bool m_isDragging = false;
            juce::Point<int> m_dragStartPosition;
            int m_originalTrackX = 0;

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
            database::EnvelopePoint m_originalEnvelopePoint;
            
            // For cue/attach marker dragging
            MarkerType m_draggedMarker = MarkerType::None;
            MarkerType m_hoveredMarker = MarkerType::None;
            database::MixTrack m_originalMixTrack;
            
           
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixTrackComponent)
        };
    } // namespace ui
} // namespace jucyaudio
