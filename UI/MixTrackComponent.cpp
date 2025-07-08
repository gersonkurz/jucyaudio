#include "BinaryData.h"
#include <UI/MixTrackComponent.h>
#include <UI/TimelineComponent.h>
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

        void MixTrackComponent::mouseDown(const juce::MouseEvent &event)
        {
            if (event.mods.isLeftButtonDown())
            {
                // Tell the timeline that this track was selected
                if (auto *timeline = findParentComponentOfClass<TimelineComponent>())
                {
                    timeline->setSelectedTrack(this);

                    // Also set playhead position based on where we clicked within the track
                    auto localClick = event.position;
                    auto trackBounds = getBounds();
                    double clickTime = (trackBounds.getX() + localClick.x) / timeline->getPixelsPerSecond();
                    timeline->setCurrentTimePosition(clickTime);
                }
            }
        }
        void MixTrackComponent::resized()
        {
            auto bounds = getLocalBounds();
            // Place the label in the top section
            m_infoLabel.setBounds(bounds.removeFromTop(textSectionHeight).reduced(4, 0));
        }

        // In MixTrackComponent.cpp
        bool MixTrackComponent::isSelected() const
        {
            // Get parent timeline and check if we're the selected track
            if (auto *timeline = findParentComponentOfClass<TimelineComponent>())
            {
                return timeline->getSelectedTrack() == this;
            }
            return false;
        }

        void MixTrackComponent::paint(juce::Graphics &g)
        {
            auto &lf = getLookAndFeel();
            auto bounds = getLocalBounds();

            // Background color - different if selected
            juce::Colour bgColor = lf.findColour(juce::TextEditor::backgroundColourId);
            if (isSelected())
            {
                bgColor = bgColor.brighter(0.2f); // Slightly brighter when selected
            }

            g.setColour(bgColor);
            g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

            auto waveformArea = bounds.removeFromBottom(waveformSectionHeight);
            g.setColour(lf.findColour(juce::Slider::thumbColourId));
            
            // Selection border
            if (isSelected())
            {
                g.setColour(juce::Colours::orange); // Or theme color
                g.drawRoundedRectangle(bounds.toFloat().reduced(1), 4.0f, 2.0f);
            }
            
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