#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/TrackLibrary.h>
#include <UI/DynamicColumnManager.h>
#include <UI/TimelineComponent.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <Audio/MixProjectLoader.h>

namespace jucyaudio
{
    namespace ui
    {
        class MixEditorComponent : public juce::Component
        {
        public:
            MixEditorComponent();

            void paint(juce::Graphics &g) override;
            void resized() override;

            void loadMix(MixId mixId);
            void forceRefresh();
            void setTrackDeletionCallback(std::function<void(TrackId)> callback);
            void setPlaybackCallback(std::function<void(const juce::File &, double)> callback);
            void setSeekCallback(std::function<void(double)> callback);

            auto &getTimeline()
            {
                return m_timeline;
            }

        private:
            juce::AudioFormatManager m_formatManager;
            juce::AudioThumbnailCache m_thumbnailCache{5}; // 5 items in the cache

            TimelineComponent m_timeline;
            juce::Viewport m_viewport;
            audio::MixProjectLoader m_mixProjectLoader;
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixEditorComponent)
        };

    } // namespace ui
} // namespace jucyaudio
