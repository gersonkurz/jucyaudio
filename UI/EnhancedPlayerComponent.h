#pragma once

#include <UI/PlaybackController.h>
#include <Database/Includes/TrackMarker.h>
#include <Database/Includes/Constants.h>
#include <functional>
#include <optional>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace jucyaudio
{
    namespace ui
    {
        /**
         * @brief Enhanced audio player component with two-row layout
         *
         * Top row (70%): Transport controls and waveform display
         * Bottom row (30%): Repeat, shuffle, volume, and time displays
         */
        class EnhancedPlayerComponent : public juce::Component, 
                                       public juce::ChangeListener
        {
        public:
            EnhancedPlayerComponent(PlaybackController &controller, 
                                  juce::AudioFormatManager &formatManager,
                                  juce::AudioThumbnailCache &thumbnailCache);
            ~EnhancedPlayerComponent() override;

            void paint(juce::Graphics &g) override;
            void resized() override;

            // Called by TimerMultiplexer for updating UI
            void updatePlaybackPosition();
            
            // ChangeListener callback for thumbnail loading
            void changeListenerCallback(juce::ChangeBroadcaster* source) override;

            // File loading (with optional track ID for marker loading)
            void loadFile(const juce::File& file, std::optional<TrackId> trackId = std::nullopt);
            
            // Marker management
            void setMarkers(const std::vector<database::TrackMarker>& markers);
            
            // Callbacks for external control
            std::function<void(TrackId, std::chrono::milliseconds, bool isNewMarker)> onMarkerAction;

        private:
            // Waveform Display Component
            class WaveformDisplay : public juce::Component, 
                                     public juce::ChangeListener,
                                     public juce::TooltipClient
            {
            public:
                WaveformDisplay(juce::AudioFormatManager& formatManager, 
                              juce::AudioThumbnailCache& thumbnailCache);
                ~WaveformDisplay() override;
                
                void paint(juce::Graphics& g) override;
                void mouseDown(const juce::MouseEvent& event) override;
                void mouseMove(const juce::MouseEvent& event) override;
                void mouseExit(const juce::MouseEvent& event) override;
                void changeListenerCallback(juce::ChangeBroadcaster* source) override;
                
                // TooltipClient
                juce::String getTooltip() override { return m_currentTooltip; }
                
                void loadFile(const juce::File& file);
                void setPlaybackPosition(double position);
                void setMarkers(const std::vector<database::TrackMarker>& markers);
                const std::vector<database::TrackMarker>& getMarkers() const { return m_markers; }
                
                std::function<void(double)> onSeek;
                std::function<void(std::chrono::milliseconds)> onMarkerClicked;
                
            private:
                juce::AudioThumbnail m_thumbnail;
                double m_playbackPosition{0.0};
                bool m_fileLoaded{false};
                std::vector<database::TrackMarker> m_markers;
                std::optional<size_t> m_hoveredMarkerIndex;
                juce::String m_currentTooltip;
                
                // Helper to convert marker position to screen X coordinate
                int markerPositionToScreenX(const database::TrackMarker& marker) const;
                std::optional<size_t> hitTestMarker(juce::Point<int> pos) const;
                juce::String formatMarkerPosition(std::chrono::milliseconds position) const;
            };
            
            // Top row components
            juce::DrawableButton m_stopButton{"Stop", juce::DrawableButton::ImageFitted};
            juce::DrawableButton m_playButton{"Play", juce::DrawableButton::ImageFitted};
            juce::DrawableButton m_pauseButton{"Pause", juce::DrawableButton::ImageFitted};

            // Waveform display
            WaveformDisplay m_waveformDisplay;

            // Bottom row components
            juce::Label m_speakerIcon;
            juce::Slider m_volumeSlider;
            juce::Label m_currentTimeLabel;
            juce::Label m_totalTimeLabel;

            // Internal state
            PlaybackController &m_playbackController;
            juce::AudioFormatManager &m_formatManager;
            juce::AudioThumbnailCache &m_thumbnailCache;
            std::optional<TrackId> m_currentTrackId;

            // Helper methods
            void updateTransportButtons();
            void updateTimeDisplays();
            void updateVolumeIcon(float gain);
            void loadButtonIcons();
            void setupButtons();
            void setupVolumeControl();

            // Button callbacks
            void playButtonClicked();
            void pauseButtonClicked();
            void stopButtonClicked();

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnhancedPlayerComponent)
        };
    } // namespace ui
} // namespace jucyaudio