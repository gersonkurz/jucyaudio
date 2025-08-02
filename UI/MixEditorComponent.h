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
#if MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE
#include <Audio/MixPlaybackEngine.h>
#endif

namespace jucyaudio
{
    namespace ui
    {
        class MixEditorComponent : public juce::Component
        {
        public:
            MixEditorComponent();
            ~MixEditorComponent() override;

            void paint(juce::Graphics &g) override;
            void resized() override;

            void loadMix(database::MixNode* node);
            void unloadMix();
            void forceRefresh();

            void setPlaybackCallback(std::function<void(const juce::File &, double)> callback);
            void setSeekCallback(std::function<void(double)> callback);
            void setMixPlaybackCallback(std::function<void(double)> callback);

            auto &getTimeline()
            {
                return m_timeline;
            }

        private:
            void updateCueAttachInData(TrackId trackId, const database::MixTrack& updatedTrack);
            void updateEnvelopeInData(TrackId trackId, const std::vector<database::EnvelopePoint>& points);
            void saveMixChanges();
            void handleMixPlayback(double startTime, bool alwaysPlay = false);
            void startMixPlayback();
            void stopMixPlayback();
            void updatePlaybackPosition();

            juce::AudioFormatManager m_formatManager;
            juce::AudioThumbnailCache m_thumbnailCache{5}; // 5 items in the cache

            TimelineComponent m_timeline;
            juce::Viewport m_viewport;
            database::MixNode *m_node{nullptr};
            
            // Mix playback
#if MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE
            std::unique_ptr<audio::MixPlaybackEngine> m_mixPlaybackEngine;
            std::unique_ptr<juce::AudioDeviceManager> m_audioDeviceManager;
#endif
            bool m_isPlaying{false};
            
            // Timer for updating playback position
            class PlaybackTimer : public juce::Timer
            {
            public:
                MixEditorComponent* owner;
                void timerCallback() override 
                { 
                    if (owner) owner->updatePlaybackPosition(); 
                }
            } m_playbackTimer;
            
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixEditorComponent)
        };

    } // namespace ui
} // namespace jucyaudio
