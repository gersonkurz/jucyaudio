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
        class MixEditorComponent : public juce::Component
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

            void setPlaybackCallback(std::function<void(const juce::File &, double)> callback);
            void setSeekCallback(std::function<void(double)> callback);
            void setMixPlaybackCallback(std::function<void(double)> callback);
            void setOnMixPlaybackStarting(std::function<void()> callback);
            void setOnMixPlaybackStopped(std::function<void()> callback);

            auto &getTimeline()
            {
                return m_timeline;
            }
            
            bool isMixPlaying() const { return m_isPlaying; }
            void stopPlayback() { if (m_isPlaying) stopMixPlayback(); }

            void handleDeleteSelectedTrack();

        private:
            void updateCueAttachInData(int orderInMix, const database::MixTrack& updatedTrack);
            void updateEnvelopeInData(int orderInMix, const std::vector<database::EnvelopePoint>& points);
            void saveMixChanges();
            void handleMixPlayback(double startTime, bool alwaysPlay = false);
            void startMixPlayback();
            void stopMixPlayback();
            void updatePlaybackPosition();

            juce::AudioFormatManager m_formatManager;
            juce::AudioThumbnailCache m_thumbnailCache{200}; // 200 items in the cache - enough for large mixes

            TimelineComponent m_timeline;
            juce::Viewport m_viewport;
            database::MixNode *m_node{nullptr};
            
            // Mix playback
            std::unique_ptr<audio::MixPlaybackEngine> m_mixPlaybackEngine;
            std::unique_ptr<juce::AudioDeviceManager> m_audioDeviceManager;
            bool m_isPlaying{false};
            std::function<void()> m_onMixPlaybackStarting;
            std::function<void()> m_onMixPlaybackStopped;
            
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
