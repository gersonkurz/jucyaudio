#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/TrackLibrary.h>
#include <Database/Nodes/MixNode.h>
#include <UI/DynamicColumnManager.h>
#include <UI/TimelineComponent.h>
#include <UI/MarkerRulerComponent.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <Audio/MixProjectLoader.h>
#include <Audio/MixPlaybackEngine.h>
#include <memory>

namespace jucyaudio
{
    namespace ui
    {
        // Forward declarations
        class PlaybackController;
        class VirtualTimelineComponent;
        
        // Lightweight overlay component that only draws the playhead
        class PlayheadOverlay : public juce::Component
        {
        public:
            PlayheadOverlay() { setInterceptsMouseClicks(false, false); }
            
            void setPlayheadPosition(double positionSeconds, double pixelsPerSecond)
            {
                if (m_positionSeconds != positionSeconds || m_pixelsPerSecond != pixelsPerSecond)
                {
                    // Calculate old and new x positions
                    const int oldX = (m_positionSeconds >= 0) ? static_cast<int>(m_positionSeconds * m_pixelsPerSecond) : -1;
                    const int newX = (positionSeconds >= 0) ? static_cast<int>(positionSeconds * pixelsPerSecond) : -1;
                    
                    m_positionSeconds = positionSeconds;
                    m_pixelsPerSecond = pixelsPerSecond;
                    
                    // Only repaint the areas that changed
                    if (oldX >= 0)
                        repaint(oldX - 7, 0, 14, getHeight());
                    if (newX >= 0)
                        repaint(newX - 7, 0, 14, getHeight());
                }
            }
            
            void paint(juce::Graphics& g) override
            {
                if (m_positionSeconds >= 0.0 && m_pixelsPerSecond > 0)
                {
                    const float playheadX = static_cast<float>(m_positionSeconds * m_pixelsPerSecond);
                    
                    // Draw the red playhead line
                    g.setColour(juce::Colours::red);
                    g.fillRect(playheadX - 1.0f, 0.0f, 2.0f, static_cast<float>(getHeight()));
                    
                    // Draw triangle at top
                    juce::Path playheadMarker;
                    playheadMarker.addTriangle(playheadX - 6, 0, playheadX + 6, 0, playheadX, 12);
                    g.fillPath(playheadMarker);
                }
            }
            
        private:
            double m_positionSeconds = -1.0;
            double m_pixelsPerSecond = 100.0;
        };
        
        class MixEditorComponent : public juce::Component, 
                                    private juce::ScrollBar::Listener
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
        
            // Get the current mix node
            database::MixNode* getCurrentMixNode() const { return m_node; }

            auto &getTimeline()
            {
                return m_timeline;
            }
            
            // Playback is now handled by PlaybackController

            void handleDeleteSelectedTrack();
            void updatePlayhead();

        private:
            void updateCueAttachInData(int orderInMix, const database::MixTrack& updatedTrack);
            void updateEnvelopeInData(int orderInMix, const std::vector<database::EnvelopePoint>& points);
            void saveMixChanges();
            void handleMixPlayback(double startTime, bool alwaysPlay = false);
            // Called by TimerMultiplexer for smooth playhead updates
            void loadMixMarkers();
            void saveMixMarker(const database::MixMarker& marker);
            void handleMarkerClick(MarkerId markerId);
            void handleMarkerAdd(std::chrono::milliseconds position);
            
            // ScrollBar::Listener callbacks
            void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override;
            void updatePlayheadOverlayPosition();
            
            // Virtual timeline methods
            void setupVirtualTimeline();
            bool isUsingVirtualTimeline() const { return m_useVirtualTimeline; }

            juce::AudioFormatManager m_formatManager;
            juce::AudioThumbnailCache m_thumbnailCache{200}; // 200 items in the cache - enough for large mixes

            MarkerRulerComponent m_markerRuler;
            TimelineComponent m_timeline;
            std::unique_ptr<VirtualTimelineComponent> m_virtualTimeline;
            PlayheadOverlay m_playheadOverlay;  // Separate component for playhead
            juce::Viewport m_viewport;
            database::MixNode *m_node{nullptr};
            
            // Virtual timeline toggle
            bool m_useVirtualTimeline{true};
            
            // Playback controller reference
            PlaybackController* m_playbackController{nullptr};
            
            // Scrolling detection to avoid update conflicts
            int64_t m_lastScrollTime{0};
            static constexpr int64_t SCROLL_PAUSE_DURATION_MS = 100; // Pause updates for 100ms after scroll
            
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixEditorComponent)
        };

    } // namespace ui
} // namespace jucyaudio
