#include <Database/Includes/INavigationNode.h>
#include <UI/MainComponent.h>
#include <UI/MixEditorComponent.h>
#include <Utils/StringWriter.h>
#include <Utils/UiUtils.h>

namespace jucyaudio
{
    namespace ui
    {
        MixEditorComponent::MixEditorComponent()
            : m_timeline{m_formatManager, m_thumbnailCache}
        {
            m_formatManager.registerBasicFormats();

            // addAndMakeVisible(m_timeline);
            //  Set the timeline as the component to be viewed by the viewport.
            m_viewport.setViewedComponent(&m_timeline, false); // false = don't delete when replaced

            // We want both horizontal and vertical scrollbars if needed.
            m_viewport.setScrollBarsShown(true, true);

            addAndMakeVisible(m_viewport);
        }

        void MixEditorComponent::forceRefresh()
        {
            m_timeline.repaint();
            m_viewport.repaint();
            resized(); // Recalculate viewport content
        }

        void MixEditorComponent::setTrackDeletionCallback(std::function<void(TrackId)> callback)
        {
            m_timeline.onTrackDeleted = callback;
        }

        void MixEditorComponent::setPlaybackCallback(std::function<void(const juce::File &, double)> callback)
        {
            m_timeline.onPlaybackRequested = callback;
        }

        void MixEditorComponent::setSeekCallback(std::function<void(double)> callback)
        {
            m_timeline.onSeekRequested = callback;
        }

        void MixEditorComponent::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ListBox::backgroundColourId).darker());
        }

        void MixEditorComponent::loadMix(MixId mixId)
        {
            spdlog::info("Loading mix with ID: {}", mixId);
            spdlog::info("Existing tracks size before loadMix(): {}", m_mixProjectLoader.getMixTracks().size());
            m_mixProjectLoader.loadMix(mixId);
            m_timeline.populateFrom(m_mixProjectLoader);
            spdlog::info("Mix loaded with {} tracks", m_mixProjectLoader.getMixTracks().size());
        }

        void MixEditorComponent::resized()
        {
            // The viewport now fills the entire editor area.
            m_viewport.setBounds(getLocalBounds());
        }
    } // namespace ui
} // namespace jucyaudio
