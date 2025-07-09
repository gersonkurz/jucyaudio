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
            static constexpr int waveformSectionHeight = 60;
            static constexpr int totalHeight = textSectionHeight + waveformSectionHeight;

            MixTrackComponent(const database::MixTrack &mixTrack, const database::TrackInfo &trackInfo, juce::AudioFormatManager &formatManager,
                              juce::AudioThumbnailCache &thumbnailCache);

            ~MixTrackComponent() override;

            void paint(juce::Graphics &g) override;
            void resized() override; // We'll add this to position our label

            void changeListenerCallback(juce::ChangeBroadcaster *source) override;
            bool isSelected() const;
            void drawVolumeEnvelope(juce::Graphics &g, juce::Rectangle<int> area);

            std::function<void(TrackId, const std::vector<database::EnvelopePoint>&)> onEnvelopeChanged;

        private:
            std::optional<size_t> hitTestEnvelopePoint(juce::Point<int> mousePos) const;
            juce::Point<int> envelopePointToScreenPosition(const database::EnvelopePoint &point) const;
            database::EnvelopePoint screenPositionToEnvelopePoint(juce::Point<int> screenPos) const;
            void constrainEnvelopePoint(size_t pointIndex, database::EnvelopePoint &point) const;
            void mouseDown(const juce::MouseEvent &event) override;
            void mouseDrag(const juce::MouseEvent &event) override;
            void mouseUp(const juce::MouseEvent &event) override;
            void mouseMove(const juce::MouseEvent &event) override;
            void setTopLeftPositionWithLogging(int newX, int newY);

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
            juce::ComponentDragger m_dragger;
            HorizontalOnlyConstrainer m_constrainer;

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
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixTrackComponent)
        };
    } // namespace ui
} // namespace jucyaudio
