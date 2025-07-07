#include "BinaryData.h"
#include <UI/MixTrackComponent.h>
#include <Utils/AssortedUtils.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        MixTrackComponent::MixTrackComponent(const database::MixTrack &mixTrack, const database::TrackInfo &trackInfo, juce::AudioFormatManager &formatManager,
                                             juce::AudioThumbnailCache &thumbnailCache)
            : m_mixTrack{mixTrack},
              m_trackInfo{trackInfo},
              m_thumbnail{512, formatManager, thumbnailCache}
        {
            // Setup the info label
            juce::String bpmText = trackInfo.bpm.has_value() ? juce::String(trackInfo.bpm.value() / 100.0, 1) + " BPM" : "--- BPM";

            juce::String infoText = juce::String(trackInfo.title) + " (" + bpmText + ")";
            m_infoLabel.setText(infoText, juce::dontSendNotification);
            m_infoLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(14.0f)}.boldened());
            m_infoLabel.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(m_infoLabel);

            // Load the thumbnail source
            m_thumbnail.setSource(new juce::FileInputSource(juce::File(trackInfo.filepath.string())));
            m_thumbnail.addChangeListener(this);
        }

        MixTrackComponent::~MixTrackComponent()
        {
            m_thumbnail.removeChangeListener(this);
        }

        void MixTrackComponent::resized()
        {
            auto bounds = getLocalBounds();
            // Place the label in the top section
            m_infoLabel.setBounds(bounds.removeFromTop(textSectionHeight).reduced(4, 0));
        }

        void MixTrackComponent::paint(juce::Graphics &g)
        {
            auto &lf = getLookAndFeel();
            auto bounds = getLocalBounds();

            // The background for the entire component
            g.setColour(lf.findColour(juce::TextEditor::backgroundColourId));
            g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

            auto waveformArea = bounds.removeFromBottom(waveformSectionHeight);
            g.setColour(lf.findColour(juce::Slider::thumbColourId));

            // --- The Mono Waveform Fix ---
            // Instead of drawing all channels, we get the number of channels from the thumbnail
            // and then call drawChannel for each, but we tell it to draw them on top of each other.
            // A simpler way is to just draw channel 0. For most stereo music, the difference
            // between L and R is not visually significant at this zoom level.
            // The most robust way is to ask the thumbnail to give us a mono representation.
            // However, the AudioThumbnail itself doesn't do mixing. It just draws what it's given.

            // Let's go with the simplest effective solution: draw only the left channel (channel 0).
            // This is visually clean and computationally cheap.
            m_thumbnail.drawChannel(g, waveformArea.reduced(2),
                                    0.0,                          // start time
                                    m_thumbnail.getTotalLength(), // end time
                                    0,                            // channel index to draw (0 = Left)
                                    1.0f);                        // vertical zoom
        }

        void MixTrackComponent::changeListenerCallback(juce::ChangeBroadcaster *source)
        {
            if (source == &m_thumbnail)
            {
                repaint();
            }
        }
    } // namespace ui
} // namespace jucyaudio