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
        class MixTrackComponent : public juce::Component,
                                  public juce::ChangeListener // To receive updates from the thumbnail
        {
        public:
            // Takes the data it needs to draw itself.
            MixTrackComponent(const database::MixTrack &mixTrack, const database::TrackInfo &trackInfo, juce::AudioFormatManager& formatManager, juce::AudioThumbnailCache &thumbnailCache);

            ~MixTrackComponent() override;

            void paint(juce::Graphics &g) override;

            // From juce::ChangeListener, called when the thumbnail has new data to draw.
            void changeListenerCallback(juce::ChangeBroadcaster *source) override;

        private:
            const database::MixTrack &m_mixTrack;   // Non-owning reference to the model data
            const database::TrackInfo &m_trackInfo; // Non-owning reference

            juce::AudioThumbnail m_thumbnail; // Manages the waveform drawing

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixTrackComponent)
        };
    } // namespace ui
} // namespace jucyaudio
