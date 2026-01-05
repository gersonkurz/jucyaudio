#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <Audio/Equalizer.h>
#include <Audio/Reverb.h>
#include <Audio/AudioVisualizerFIFO.h>
#include <Database/Includes/TrackInfo.h>
#include <spdlog/spdlog.h>
#include <memory>
#include <optional>
#include <random>

namespace jucyaudio
{
    namespace audio
    {
        class MixPlaybackEngine;
        class MixProjectLoader;
    }

    namespace ui
    {
        /**
         * @brief Repeat mode for playlist playback
         */
        enum class RepeatMode
        {
            None,   // Stop after last track
            One,    // Repeat current track
            All     // Repeat entire playlist
        };

        /**
         * @brief Source of the current playlist (for UI context)
         */
        enum class PlaylistSource
        {
            None,         // No playlist active
            Folder,       // Tracks from a folder
            Selection,    // User-selected tracks
            SearchResult  // Tracks from a search
        };

        /**
         * @brief Queue of tracks for playlist playback
         */
        struct PlaylistQueue
        {
            std::vector<database::TrackInfo> tracks;
            size_t currentIndex = 0;

            RepeatMode repeatMode = RepeatMode::None;
            bool shuffleEnabled = false;
            std::vector<size_t> shuffleOrder;  // Indices into tracks when shuffle is on

            PlaylistSource source = PlaylistSource::None;
            std::string sourcePath;  // e.g., folder path for context

            bool isEmpty() const { return tracks.empty(); }
            size_t size() const { return tracks.size(); }

            const database::TrackInfo* getCurrentTrack() const
            {
                if (tracks.empty()) return nullptr;
                size_t idx = shuffleEnabled && !shuffleOrder.empty()
                    ? shuffleOrder[currentIndex]
                    : currentIndex;
                return (idx < tracks.size()) ? &tracks[idx] : nullptr;
            }

            void clear()
            {
                tracks.clear();
                shuffleOrder.clear();
                currentIndex = 0;
                source = PlaylistSource::None;
                sourcePath.clear();
            }
        };

        class PlaybackController final : public juce::ChangeBroadcaster
        {
        public:
            // The 7 states from player-ears.md
            enum class PlayerState
            {
                Silence,              // Nothing loaded, nothing playing
                SilenceTrackLoaded,   // Track loaded but not playing
                TrackPlaying,         // Single track is playing
                TrackPaused,          // Single track is paused (position preserved)
                SilenceMixLoaded,     // Mix loaded but not playing
                MixPlaying,           // Mix is playing
                MixPaused             // Mix is paused (position preserved)
            };

            PlaybackController();
            ~PlaybackController();

            // --- AudioDeviceIOCallback methods to be called by MainComponent ---
            void prepareToPlay(int samplesPerBlockExpected, double sampleRate);
            void getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill);
            void releaseResources();
            bool loadAndPlayFileFromPosition(const juce::File &audioFile, double startPositionSeconds);

            // --- Playback Control Methods ---
            // Returns true if loading was successful and playback started/is starting.
            bool loadAndPlayFile(const juce::File &audioFile);
            
            // Load and play a mix
            bool loadMix(audio::MixProjectLoader* mixLoader);
            void playMixFrom(double absoluteTimelineSeconds);
            
            // State query
            PlayerState getState() const { return m_currentState; }

            // Main control methods - behavior depends on current state
            void play();    // Resume from pause or start from beginning
            void pause();   // Pause and preserve position
            void stop();    // Stop and reset position to 0

            void seek(double positionSeconds);  // Seek within current track/mix
            void setGain(float newGain);
            
            // --- Playlist Management ---
            // Set a playlist and optionally start playing from a specific index
            void setPlaylist(const std::vector<database::TrackInfo>& tracks,
                           size_t startIndex = 0,
                           PlaylistSource source = PlaylistSource::Folder,
                           const std::string& sourcePath = "");
            void clearPlaylist();
            const PlaylistQueue& getPlaylistQueue() const { return m_playlist; }
            bool hasPlaylist() const { return !m_playlist.isEmpty(); }

            // Track navigation
            bool nextTrack();      // Returns false if at end and no repeat
            bool previousTrack();  // Returns false if at beginning
            void handleTrackEnded();  // Called when transport reaches end of track

            // Get current track info (works for both single track and playlist)
            const database::TrackInfo* getCurrentTrackInfo() const;
            TrackId getCurrentTrackId() const;

            // Playback modes (now uses RepeatMode enum)
            void setRepeatMode(RepeatMode mode);
            RepeatMode getRepeatMode() const { return m_playlist.repeatMode; }
            bool isRepeatEnabled() const { return m_playlist.repeatMode != RepeatMode::None; }
            void setShuffleMode(bool enabled);
            bool getShuffleMode() const { return m_playlist.shuffleEnabled; }

            // --- State Query Methods ---
            bool isPlaying() const;
            bool isMixMode() const;  // True if in any mix state
            double getCurrentPositionSeconds() const;  // Absolute timeline position
            double getLengthInSeconds() const;
            juce::String getCurrentFilepath() const { return m_currentFile.getFullPathName(); }

            // UI State getters for any UI component to query
            bool canPlay() const;
            bool canStop() const;
            bool canPause() const;
            bool isEffectivelyPlaying() const;
            
            // Callback for state changes
            std::function<void(PlayerState)> onStateChanged;

            // Callback when current track changes (for UI to update highlight, etc.)
            // Parameters: trackId of new track, index in playlist (or 0 if single track)
            std::function<void(TrackId, size_t)> onCurrentTrackChanged;

            // --- Access to TransportSource for MainComponent to be a ChangeListener ---
            // This allows MainComponent to listen for when the transport source itself stops (e.g., end of file)
            juce::AudioTransportSource &getTransportSource()
            {
                return m_audioTransportSource;
            }

            // VU Meter processing
            void processAudioBlock(const juce::AudioBuffer<float>& buffer);
            float getPeakLeft() const { return m_peakLeft.load(); }
            float getPeakRight() const { return m_peakRight.load(); }
            
            // Master EQ Control
            void updateMasterEQ(const audio::model::EQSettings& settings);
            audio::model::EQSettings getCurrentEQSettings() const { return m_currentEQSettings; }
            
            // Master Reverb Control
            void updateMasterReverb(const audio::model::ReverbSettings& settings);
            audio::model::ReverbSettings getCurrentReverbSettings() const { return m_currentReverbSettings; }

            // Visualizer FIFO - connects audio to visualizer
            void setVisualizerFIFO(audio::AudioVisualizerFIFO* fifo);

        private:
            void changeState(PlayerState newState);
            void loadFileInternal(const juce::File &audioFile);
            void unloadAudioSource();
            void unloadMix();

            // Audio components
            juce::AudioFormatManager m_audioFormatManager;
            std::unique_ptr<juce::AudioFormatReaderSource> m_currentAudioFileSource;
            juce::AudioTransportSource m_audioTransportSource;
            audio::Equalizer m_masterEqualizer;
            audio::Reverb m_masterReverb;
            
            // Current effect settings
            audio::model::EQSettings m_currentEQSettings;
            audio::model::ReverbSettings m_currentReverbSettings;
            
            // Mix playback support
            std::unique_ptr<audio::MixPlaybackEngine> m_mixPlaybackEngine;
            audio::MixProjectLoader* m_currentMixLoader{nullptr};
            
            // Device state
            double m_deviceSampleRate{0.0};
            int m_deviceBlockSize{0};
            bool m_isDevicePrepared{false};
            
            // Current state
            PlayerState m_currentState{PlayerState::Silence};
            juce::File m_currentFile;
            
            // Position tracking (absolute timeline position in seconds)
            double m_pausedPosition{0.0};

            // VU Meter atomics
            std::atomic<float> m_peakLeft{0.0f};
            std::atomic<float> m_peakRight{0.0f};

            // To prevent re-entrancy or rapid state changes from UI/callbacks
            std::atomic<bool> m_isCurrentlyChangingState{false};

            // Playlist queue (unified playlist management)
            PlaylistQueue m_playlist;

            // Single track info (when playing without playlist)
            std::optional<database::TrackInfo> m_singleTrackInfo;

            // Visualizer FIFO (non-owning pointer, fed after EQ/Reverb)
            audio::AudioVisualizerFIFO* m_visualizerFIFO{nullptr};

            // Random engine for shuffle
            std::mt19937 m_randomEngine{std::random_device{}()};

            // Helper to generate shuffle order
            void generateShuffleOrder();

            // Helper to play track at current playlist index
            bool playCurrentPlaylistTrack();

            // Notify UI of track change
            void notifyTrackChanged();
        };
    } // namespace ui
} // namespace jucyaudio