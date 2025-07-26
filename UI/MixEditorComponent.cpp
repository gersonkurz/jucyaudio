#include <Database/Includes/INavigationNode.h>
#include <UI/MainComponent.h>
#include <UI/MixEditorComponent.h>
#include <Utils/StringWriter.h>
#include <Utils/UiUtils.h>
#include <Database/TrackLibrary.h>

namespace jucyaudio
{
    namespace ui
    {

        MixEditorComponent::MixEditorComponent()
            : m_timeline{m_formatManager, m_thumbnailCache},
              m_node{nullptr}
        {
            m_formatManager.registerBasicFormats();

            // Set the timeline as the component to be viewed by the viewport.
            m_viewport.setViewedComponent(&m_timeline, false); // false = don't delete when replaced
            m_viewport.setScrollBarsShown(true, true);
            addAndMakeVisible(m_viewport);

            // Set up the new drag-related callbacks
            m_timeline.onTrackPositionChanged = [this](TrackId trackId, std::chrono::milliseconds newStartTime)
            {
                updateTrackPositionInData(trackId, newStartTime);
            };

            m_timeline.onMixChanged = [this]()
            {
                saveMixChanges();
            };
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

        void MixEditorComponent::unloadMix()
        {
            if (m_node)
            {
                m_node->release(REFCOUNT_DEBUG_ARGS);
                m_node = nullptr;
            }
        }

        void MixEditorComponent::loadMix(database::MixNode *node)
        {
            assert(node != nullptr && "MixNode should not be null in loadMix()");
            if (m_node)
            {
                m_node->release(REFCOUNT_DEBUG_ARGS); // Release previous node if any
            }
            m_node = node; // Take ownership of the new node
            node->retain(REFCOUNT_DEBUG_ARGS); // Retain the node to ensure it stays valid
            node->refreshCache(false);
            m_timeline.populateFrom(node->getMixProjectLoader());
        }

        void MixEditorComponent::resized()
        {
            // The viewport now fills the entire editor area.
            m_viewport.setBounds(getLocalBounds());
        }


        // Add new methods to MixEditorComponent.cpp
        void MixEditorComponent::updateTrackPositionInData(TrackId trackId, std::chrono::milliseconds newStartTime)
        {
            if (!m_node)
            {
                spdlog::error("MixEditorComponent::updateTrackPositionInData - No mix node loaded");
                return;
            }
            spdlog::info("Updating position for track {} to {}ms", trackId, newStartTime.count());

            // Get non-const access to the mix tracks
            auto &mixTracks = m_node->getMixProjectLoader().getMixTracks(); // This method needs to be added

            // Find and update the track
            for (auto &track : mixTracks)
            {
                if (track.trackId == trackId)
                {
                    track.mixStartTime = newStartTime;
                    spdlog::info("Updated track {} mixStartTime to {}", trackId, newStartTime.count());
                    break;
                }
            }
        }

        void MixEditorComponent::saveMixChanges()
        {
            if (!m_node)
            {
                spdlog::error("MixEditorComponent::saveMixChanges - No mix node loaded");
                return;
            }
            spdlog::info("Saving mix changes to database");

            // Get the current mix info and tracks
            auto &mixProjectLoader = m_node->getMixProjectLoader();
            auto mixId = mixProjectLoader.getMixId();
            auto &mixTracks = mixProjectLoader.getMixTracks();

            // Get mix info from database
            auto mixInfo = database::theTrackLibrary.getMixManager().getMix(mixId);

            // Save changes back to database
            if (database::theTrackLibrary.getMixManager().createOrUpdateMix(mixInfo, mixTracks))
            {
                spdlog::info("Successfully saved mix changes");
            }
            else
            {
                spdlog::error("Failed to save mix changes");
            }
        }
    } // namespace ui
} // namespace jucyaudio
