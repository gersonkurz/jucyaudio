#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/TrackMarker.h>
#include <UI/PlaybackController.h>
#include <functional>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace jucyaudio
{
    namespace ui
    {
        /**
         * @brief Enhanced audio player component with two-row layout
         *
         * Top row: Transport controls and waveform display
         * Bottom row: Repeat, shuffle, volume, and time displays
         */
        class EnhancedPlayerComponent : public juce::Component, public juce::ChangeListener
        {
        public:
            EnhancedPlayerComponent(PlaybackController &controller, juce::AudioFormatManager &formatManager, juce::AudioThumbnailCache &thumbnailCache);
            ~EnhancedPlayerComponent() override;

            void paint(juce::Graphics &g) override;
            void resized() override;

            // Called by TimerMultiplexer for updating UI
            void updatePlaybackPosition();

            // ChangeListener callback for thumbnail loading
            void changeListenerCallback(juce::ChangeBroadcaster *source) override;

            // File loading (with optional track ID for marker loading)
            void loadFile(const juce::File &file, std::string_view text, std::optional<TrackId> trackId = std::nullopt);

            // Marker management
            void setMarkers(const std::vector<database::TrackMarker> &markers);

            // Volume control for media keys
            float getVolumeSliderValue() const;
            void setVolumeSliderValue(float value);

            void setRightHandPadding(int padding);

            // Callbacks for external control
            std::function<void(TrackId, std::chrono::milliseconds, bool isNewMarker)> onMarkerAction;
            std::function<void()> onNextTrack;
            std::function<void()> onPreviousTrack;

        private:
            // Waveform Display Component
            class WaveformDisplay : public juce::Component, public juce::ChangeListener, public juce::TooltipClient
            {
            public:
                WaveformDisplay(juce::AudioFormatManager &formatManager, juce::AudioThumbnailCache &thumbnailCache);
                ~WaveformDisplay() override;

                void paint(juce::Graphics &g) override;
                void mouseDown(const juce::MouseEvent &event) override;
                void mouseMove(const juce::MouseEvent &event) override;
                void mouseExit(const juce::MouseEvent &event) override;
                void changeListenerCallback(juce::ChangeBroadcaster *source) override;

                // TooltipClient
                juce::String getTooltip() override
                {
                    return m_currentTooltip;
                }

                void loadFile(const juce::File &file);
                void setPlaybackPosition(double position);
                void setMarkers(const std::vector<database::TrackMarker> &markers);
                const std::vector<database::TrackMarker> &getMarkers() const
                {
                    return m_markers;
                }

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
                int markerPositionToScreenX(const database::TrackMarker &marker) const;
                std::optional<size_t> hitTestMarker(juce::Point<int> pos) const;
                juce::String formatMarkerPosition(std::chrono::milliseconds position) const;
            };

            // Top row components (transport buttons)
            juce::DrawableButton m_prevButton{"Previous", juce::DrawableButton::ImageFitted};
            juce::DrawableButton m_stopButton{"Stop", juce::DrawableButton::ImageFitted};
            juce::DrawableButton m_playPauseButton{"PlayPause", juce::DrawableButton::ImageFitted};
            juce::DrawableButton m_nextButton{"Next", juce::DrawableButton::ImageFitted};

            // Waveform display
            WaveformDisplay m_waveformDisplay;

            // Bottom row components
            juce::DrawableButton m_volumeButton{"Volume", juce::DrawableButton::ImageFitted};
            juce::Slider m_volumeSlider;
            juce::Label m_trackInfoLabel;
            juce::Label m_currentTimeLabel;
            juce::Label m_totalTimeLabel;
            juce::DrawableButton m_repeatButton{"Repeat", juce::DrawableButton::ImageFitted};
            juce::DrawableButton m_shuffleButton{"Shuffle", juce::DrawableButton::ImageFitted};

            // Internal state
            PlaybackController &m_playbackController;
            std::optional<TrackId> m_currentTrackId;
            float m_lastVolumeBeforeMute{1.0f};
            bool m_isRepeatOn{false};
            bool m_isShuffleOn{false};
            int m_rightHandPadding{0};

            // Drawable assets for the volume button
            std::unique_ptr<juce::Drawable> m_iconPlay;
            std::unique_ptr<juce::Drawable> m_iconPause;
            std::unique_ptr<juce::Drawable> m_iconVolumeHigh;
            std::unique_ptr<juce::Drawable> m_iconVolumeLow;
            std::unique_ptr<juce::Drawable> m_iconVolumeMute;
            std::unique_ptr<juce::Drawable> m_iconRepeatOff;
            std::unique_ptr<juce::Drawable> m_iconRepeatOn;
            std::unique_ptr<juce::Drawable> m_iconShuffleOff;
            std::unique_ptr<juce::Drawable> m_iconShuffleOn;
            // Helper methods
            void updateTransportButtons();
            void updateTimeDisplays();
            void updateVolumeIcon(float gain);
            void loadButtonIcons();
            void loadVolumeIcons();
            void setupButtons();
            void setupVolumeControl();
            void volumeButtonClicked();
            void updateToggleButtons();
            void repeatButtonClicked();
            void shuffleButtonClicked();
            void setTrackInfo(const juce::String &info);

            // Button callbacks
            void playPauseButtonClicked();
            void stopButtonClicked();

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnhancedPlayerComponent)
        };
    } // namespace ui
} // namespace jucyaudio