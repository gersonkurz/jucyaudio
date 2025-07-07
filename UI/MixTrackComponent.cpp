#include "BinaryData.h"
#include <UI/MixTrackComponent.h>
#include <spdlog/spdlog.h>
#include <Utils/AssortedUtils.h>

namespace jucyaudio
{
    namespace ui
    {
        MixTrackComponent::MixTrackComponent(const database::MixTrack &mixTrack, const database::TrackInfo &trackInfo,
                                             juce::AudioFormatManager& formatManager, juce::AudioThumbnailCache &thumbnailCache)
            : m_mixTrack{mixTrack},
              m_trackInfo{trackInfo},
              // Initialize the thumbnail. 512 is the source hash, 512x512 are thumbnail dimensions.
              m_thumbnail{512, formatManager, thumbnailCache}
        {
            // Tell the thumbnail to start loading the audio data from the file.
            // This happens on a background thread automatically.
            m_thumbnail.setSource(new juce::FileInputSource(juce::File{pathToString(trackInfo.filepath)}));

            // We want to know when the thumbnail has new data so we can repaint.
            m_thumbnail.addChangeListener(this);
        }

        MixTrackComponent::~MixTrackComponent()
        {
            m_thumbnail.removeChangeListener(this);
        }

        void MixTrackComponent::paint(juce::Graphics &g)
        {
            auto &lf = getLookAndFeel();

            // Draw a background rectangle for the track segment.
            g.setColour(lf.findColour(juce::TextEditor::backgroundColourId));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
            g.setColour(lf.findColour(juce::TextEditor::outlineColourId));
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);

            // Draw the waveform.
            g.setColour(lf.findColour(juce::Slider::thumbColourId));
            m_thumbnail.drawChannels(g, getLocalBounds().reduced(2),
                                     0.0,                          // start time in source
                                     m_thumbnail.getTotalLength(), // end time in source
                                     1.0f);                        // vertical zoom factor

            // Draw the track title.
            g.setColour(lf.findColour(juce::Label::textColourId));
            g.setFont(14.0f);
            g.drawText(m_trackInfo.title, getLocalBounds().reduced(8, 0), juce::Justification::centredLeft, true);
        }

        void MixTrackComponent::changeListenerCallback(juce::ChangeBroadcaster *source)
        {
            if (source == &m_thumbnail)
            {
                // The thumbnail has loaded more data, so we need to repaint our component.
                repaint();
            }

        } // namespace ui
    } // namespace jucyaudio
}