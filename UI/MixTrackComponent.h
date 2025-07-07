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

        private:
            const database::MixTrack &m_mixTrack;
            const database::TrackInfo &m_trackInfo;

            juce::AudioThumbnail m_thumbnail;

            // A label for displaying the track info text
            juce::Label m_infoLabel;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixTrackComponent)
        };
    } // namespace ui
} // namespace jucyaudio
