#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/TrackLibrary.h>
#include <Database/Nodes/MixNode.h>
#include <UI/DynamicColumnManager.h>
#include <UI/TimelineComponent.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <Audio/MixProjectLoader.h>
#include <Audio/MixPlaybackEngine.h>

namespace jucyaudio
{
    namespace ui
    {
        // Forward declarations
        class PlaybackController;
        
        class MixEditorComponent : public juce::Component, private juce::Timer
        {
        public:
            MixEditorComponent();
            ~MixEditorComponent() override;

            void paint(juce::Graphics &g) override;
            void resized() override;
            bool keyPressed(const juce::KeyPress &key) override;

            void loadMix(database::MixNode* node);
            void unloadMix();
            void forceRefresh();

            // Set the playback controller for unified playback
            void setPlaybackController(PlaybackController* controller);
        PlaybackController* getPlaybackController() { return m_playbackController; }

            auto &getTimeline()
            {
                return m_timeline;
            }
            
            // Playback is now handled by PlaybackController

            void handleDeleteSelectedTrack();

        private:
            void updateCueAttachInData(int orderInMix, const database::MixTrack& updatedTrack);
            void updateEnvelopeInData(int orderInMix, const std::vector<database::EnvelopePoint>& points);
            void saveMixChanges();
            void handleMixPlayback(double startTime, bool alwaysPlay = false);
            void timerCallback() override;

            juce::AudioFormatManager m_formatManager;
            juce::AudioThumbnailCache m_thumbnailCache{200}; // 200 items in the cache - enough for large mixes

            TimelineComponent m_timeline;
            juce::Viewport m_viewport;
            database::MixNode *m_node{nullptr};
            
            // Playback controller reference
            PlaybackController* m_playbackController{nullptr};
            
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixEditorComponent)
        };

    } // namespace ui
} // namespace jucyaudio
