#include "BinaryData.h"
#include <Database/Includes/Constants.h>
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
                if (auto *timeline = findParentComponentOfClass<TimelineComponent>())
                {
                    timeline->setSelectedTrack(this);

                    // Calculate the time position within the track
                    auto localClick = event.position;
                    auto trackBounds = getBounds();
                    double clickTime = (trackBounds.getX() + localClick.x) / timeline->getPixelsPerSecond();
                    timeline->setCurrentTimePosition(clickTime);

                    spdlog::info("Track clicked at local x: {:.1f}, global time: {:.2f}s", localClick.x, clickTime);

                    if (event.getNumberOfClicks() == 2)
                    {
                        // Double-click: ALWAYS play this track from clicked position
                        spdlog::info("Double-click on track - requesting playback");
                        if (timeline->onPlaybackRequested)
                        {
                            const auto startTime = std::chrono::duration<double>(m_mixTrack.mixStartTime).count();
                            double trackOffset = clickTime - startTime;
                            trackOffset = juce::jlimit(0.0, std::chrono::duration<double>(m_trackInfo.duration).count(), trackOffset);

                            juce::File audioFile(m_trackInfo.filepath.string());
                            timeline->onPlaybackRequested(audioFile, trackOffset);
                        }
                    }
                    else if (event.getNumberOfClicks() == 1)
                    {
                        // Single-click: only change playback if something is already playing
                        if (timeline->onSeekRequested)
                        {
                            spdlog::info("Single-click on track - checking if we should switch playback");
                            timeline->onSeekRequested(clickTime);
                        }
                    }
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

            // Draw volume envelope on top
            drawVolumeEnvelope(g, waveformArea);
        }

        void MixTrackComponent::drawVolumeEnvelope(juce::Graphics &g, juce::Rectangle<int> area)
        {
            juce::Path volumePath;

            // Calculate key time points (all relative to track start)
            const auto trackDuration = std::chrono::duration<double>(m_trackInfo.duration).count();
            const auto fadeInStart = std::chrono::duration<double>(m_mixTrack.fadeInStart).count();
            const auto fadeInEnd = std::chrono::duration<double>(m_mixTrack.fadeInEnd).count();
            const auto fadeOutStart = std::chrono::duration<double>(m_mixTrack.fadeOutStart).count();
            const auto fadeOutEnd = std::chrono::duration<double>(m_mixTrack.fadeOutEnd).count();

            // Convert volumes to 0-1 range for rendering
            const float volStart = m_mixTrack.volumeAtStart / float(database::VOLUME_NORMALIZATION);
            const float volEnd = m_mixTrack.volumeAtEnd / float(database::VOLUME_NORMALIZATION);

            // Calculate Y positions
            const float silenceY = area.getBottom(); // 0% volume = bottom of area
            const float volStartY = area.getBottom() - (volStart * area.getHeight());
            const float volEndY = area.getBottom() - (volEnd * area.getHeight());

            // Start the path at track beginning (at silence)
            volumePath.startNewSubPath(area.getX(), silenceY);

            // Phase 1: Pre-fade-in (silence until fade-in starts)
            if (fadeInStart > 0.0)
            {
                float fadeInStartX = area.getX() + (fadeInStart / trackDuration) * area.getWidth();
                volumePath.lineTo(fadeInStartX, silenceY);
            }

            // Phase 2: Fade-in (0% to volumeAtStart%)
            if (fadeInEnd > fadeInStart)
            {
                float fadeInStartX = area.getX() + (fadeInStart / trackDuration) * area.getWidth();
                float fadeInEndX = area.getX() + (fadeInEnd / trackDuration) * area.getWidth();

                // If fadeInStart was 0, we're already at the right position
                if (fadeInStart > 0.0)
                {
                    volumePath.lineTo(fadeInStartX, silenceY); // Stay at silence until fade starts
                }
                volumePath.lineTo(fadeInEndX, volStartY); // Ramp up to volumeAtStart
            }
            else
            {
                // No fade-in, jump directly to volume level
                volumePath.lineTo(area.getX(), volStartY);
            }

            // Phase 3: Sustain period (volumeAtStart to volumeAtEnd - usually flat)
            if (fadeOutStart > fadeInEnd)
            {
                float fadeOutStartX = area.getX() + (fadeOutStart / trackDuration) * area.getWidth();
                volumePath.lineTo(fadeOutStartX, volEndY); // Sustain at volumeAtEnd level
            }

            // Phase 4: Fade-out (volumeAtEnd to 0%)
            if (fadeOutEnd > fadeOutStart)
            {
                float fadeOutStartX = area.getX() + (fadeOutStart / trackDuration) * area.getWidth();
                float fadeOutEndX = area.getX() + (fadeOutEnd / trackDuration) * area.getWidth();

                // Make sure we're at the sustain level at fade-out start
                volumePath.lineTo(fadeOutStartX, volEndY);
                // Fade down to silence
                volumePath.lineTo(fadeOutEndX, silenceY);
            }
            else
            {
                // No fade-out, maintain level to end
                volumePath.lineTo(area.getRight(), volEndY);
            }

            // Phase 5: Post-fade-out (silence to track end)
            if (fadeOutEnd < trackDuration)
            {
                volumePath.lineTo(area.getRight(), silenceY);
            }

            // Draw the envelope line
            g.setColour(juce::Colours::yellow.withAlpha(0.8f));
            g.strokePath(volumePath, juce::PathStrokeType(2.0f));

            // Optional: Add debug logging for the envelope shape
            spdlog::debug("Track {}: fadeIn[{:.1f}-{:.1f}s], sustain[{:.1f}-{:.1f}s], fadeOut[{:.1f}-{:.1f}s], volStart={:.1f}%, volEnd={:.1f}%",
                          m_mixTrack.trackId, fadeInStart, fadeInEnd, fadeInEnd, fadeOutStart, fadeOutStart, fadeOutEnd, volStart * 100, volEnd * 100);
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