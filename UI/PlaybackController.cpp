#include <UI/PlaybackController.h>
#include <Audio/MixPlaybackEngine.h>
#include <Audio/MixProjectLoader.h>
#include <Database/TrackLibrary.h>
#include <UI/Settings.h>
#include <Config/toml_backend.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        extern std::string g_strConfigFilename;
        using namespace audio;

        PlaybackController::PlaybackController()
        {
            m_audioFormatManager.registerBasicFormats();

            // Initialize mix playback engine
            m_mixPlaybackEngine = std::make_unique<MixPlaybackEngine>();

            // Load playback settings from config
            m_playlist.shuffleEnabled = config::theSettings.audioSettings.shuffleMode;
            const int repeatModeInt = config::theSettings.audioSettings.repeatMode;
            m_playlist.repeatMode = static_cast<RepeatMode>(repeatModeInt);
        }

        PlaybackController::~PlaybackController()
        {
            if (m_currentAudioFileSource != nullptr)
            {
                m_audioTransportSource.setSource(nullptr);
            }
            
            // Clean up mix engine
            if (m_mixPlaybackEngine)
            {
                m_mixPlaybackEngine->unloadMix();
            }
        }

        void PlaybackController::changeState(PlayerState newState)
        {
            if (m_currentState == newState)
                return;
                
            bool expected = false;
            if (!m_isCurrentlyChangingState.compare_exchange_strong(expected, true))
            {
                spdlog::warn("[PlaybackController] Already changing state, ignoring request");
                return;
            }

            const auto oldState = m_currentState;
            m_currentState = newState;
            
            spdlog::info("[PlaybackController] State transition: {} -> {}", 
                        static_cast<int>(oldState), static_cast<int>(newState));
            
            // Notify listeners
            if (onStateChanged)
            {
                onStateChanged(newState);
            }
            
            sendChangeMessage();
            m_isCurrentlyChangingState = false;
        }

        void PlaybackController::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
        {
            m_deviceSampleRate = sampleRate;
            m_deviceBlockSize = samplesPerBlockExpected;
            m_isDevicePrepared = true;
            
            m_audioTransportSource.prepareToPlay(samplesPerBlockExpected, sampleRate);
            
            // Prepare the master equalizer and reverb
            juce::dsp::ProcessSpec spec;
            spec.sampleRate = sampleRate;
            spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlockExpected);
            spec.numChannels = 2;
            m_masterEqualizer.prepare(spec);
            m_masterReverb.prepare(spec);
            audio::theMasterPluginChain.prepareToPlay(sampleRate, samplesPerBlockExpected);
            
            if (m_mixPlaybackEngine)
            {
                m_mixPlaybackEngine->prepareToPlay(samplesPerBlockExpected, sampleRate);
            }
            
            spdlog::info("[PlaybackController] Prepared to play - sample rate: {}, block size: {}", 
                        sampleRate, samplesPerBlockExpected);
        }

        void PlaybackController::getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill)
        {
            if (m_currentState == PlayerState::TrackPlaying)
            {
                m_audioTransportSource.getNextAudioBlock(bufferToFill);
            }
            else if (m_currentState == PlayerState::MixPlaying)
            {
                if (m_mixPlaybackEngine)
                {
                    m_mixPlaybackEngine->getNextAudioBlock(bufferToFill);
                }
                else
                {
                    bufferToFill.clearActiveBufferRegion();
                }
            }
            else
            {
                bufferToFill.clearActiveBufferRegion();
            }
            
            // Apply master EQ, reverb, and VST chain to the audio (after getting the source audio)
            if (m_currentState == PlayerState::TrackPlaying || m_currentState == PlayerState::MixPlaying)
            {
                juce::dsp::AudioBlock<float> block(*bufferToFill.buffer);
                auto subBlock = block.getSubBlock(static_cast<size_t>(bufferToFill.startSample),
                                                  static_cast<size_t>(bufferToFill.numSamples));
                m_masterEqualizer.process(subBlock);
                m_masterReverb.process(subBlock);
                juce::AudioBuffer<float> pluginBuffer{
                    bufferToFill.buffer->getArrayOfWritePointers(),
                    bufferToFill.buffer->getNumChannels(),
                    bufferToFill.startSample,
                    bufferToFill.numSamples};
                audio::theMasterPluginChain.processBlock(pluginBuffer);

                // Feed audio to visualizer FIFO after EQ/Reverb/VST chain (reflects final output)
                if (m_visualizerFIFO != nullptr)
                {
                    m_visualizerFIFO->writeFromBuffer(*bufferToFill.buffer,
                                                      bufferToFill.startSample,
                                                      bufferToFill.numSamples);
                }
            }
        }

        void PlaybackController::releaseResources()
        {
            m_audioTransportSource.releaseResources();
            m_masterEqualizer.reset();
            m_masterReverb.reset();
            audio::theMasterPluginChain.releaseResources();
            
            if (m_mixPlaybackEngine)
            {
                m_mixPlaybackEngine->releaseResources();
            }
            
            m_isDevicePrepared = false;
        }

        bool PlaybackController::loadAndPlayFile(const juce::File &audioFile)
        {
            return loadAndPlayFileFromPosition(audioFile, 0.0);
        }

        bool PlaybackController::loadAndPlayFileFromPosition(const juce::File &audioFile, double startPositionSeconds)
        {
            spdlog::info("[PlaybackController] Loading file: {}", audioFile.getFullPathName().toStdString());
            
            // Stop any mix playback
            if (isMixMode())
            {
                unloadMix();
            }
            
            loadFileInternal(audioFile);
            
            if (m_currentAudioFileSource != nullptr)
            {
                m_audioTransportSource.setPosition(startPositionSeconds);
                m_audioTransportSource.start();
                changeState(PlayerState::TrackPlaying);
                return true;
            }
            
            return false;
        }

        void PlaybackController::loadFileInternal(const juce::File &audioFile)
        {
            // First, stop and unload any existing source
            if (m_currentState == PlayerState::TrackPlaying)
            {
                m_audioTransportSource.stop();
            }
            
            m_audioTransportSource.setSource(nullptr);
            m_currentAudioFileSource.reset();
            
            // Log if file has special characters
            const auto filepath = audioFile.getFullPathName().toStdString();
            const bool hasSpecialChars = filepath.find('{') != std::string::npos || 
                                        filepath.find('}') != std::string::npos;
            
            if (hasSpecialChars) {
                spdlog::debug("Loading file with special chars: {}", filepath);
            }
            
            // Try to create a reader for the new file
            std::unique_ptr<juce::AudioFormatReader> reader(m_audioFormatManager.createReaderFor(audioFile));
            
            if (reader != nullptr)
            {
                // Log audio format details for files with special chars
                if (hasSpecialChars) {
                    spdlog::debug("Single track reader: channels={}, sampleRate={}, bitsPerSample={}, formatName={}",
                                reader->numChannels,
                                reader->sampleRate,
                                reader->bitsPerSample,
                                reader->getFormatName().toStdString());
                }
                
                m_currentFile = audioFile;
                m_currentAudioFileSource = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
                m_audioTransportSource.setSource(m_currentAudioFileSource.get(), 0, nullptr, 
                                                m_currentAudioFileSource->getAudioFormatReader()->sampleRate);
                
                // Don't loop individual tracks - repeat mode is for playlists/folders
                m_audioTransportSource.setLooping(false);
                
                changeState(PlayerState::SilenceTrackLoaded);
                spdlog::info("[PlaybackController] File loaded successfully");
            }
            else
            {
                spdlog::error("[PlaybackController] Failed to create reader for file");
                m_currentFile = juce::File();
                changeState(PlayerState::Silence);
            }
        }

        bool PlaybackController::loadMix(audio::MixProjectLoader* mixLoader)
        {
            spdlog::warn("=== GAIN CHANGE === PlaybackController::loadMix -> Entry, current state: {}", static_cast<int>(m_currentState));
            if (!mixLoader)
            {
                spdlog::error("[PlaybackController] Null mix loader provided");
                return false;
            }

            // Remember if we were in mix playback mode before reloading
            const bool wasInMixMode = (m_currentState == PlayerState::SilenceMixLoaded ||
                                       m_currentState == PlayerState::MixPlaying ||
                                       m_currentState == PlayerState::MixPaused);
            const auto previousState = m_currentState;

            spdlog::warn("=== GAIN CHANGE === wasInMixMode={}, previousState={}", wasInMixMode, static_cast<int>(previousState));

            // Stop any track playback
            if (m_currentState == PlayerState::TrackPlaying || m_currentState == PlayerState::TrackPaused)
            {
                unloadAudioSource();
            }

            // Load the mix
            m_currentMixLoader = mixLoader;

            spdlog::warn("=== GAIN CHANGE === About to call m_mixPlaybackEngine->loadMix()");
            if (m_mixPlaybackEngine->loadMix(mixLoader))
            {
                spdlog::warn("=== GAIN CHANGE === m_mixPlaybackEngine->loadMix() succeeded");

                // Apply the mix's EQ settings
                m_masterEqualizer.updateParameters(mixLoader->getMasterEQSettings());

                // Preserve the playback state if we were already in mix mode
                // This allows hot-reloading the mix while playing for gain/envelope changes
                if (wasInMixMode && (previousState == PlayerState::MixPlaying || previousState == PlayerState::MixPaused))
                {
                    // Keep the current playing/paused state
                    spdlog::warn("=== GAIN CHANGE === Preserving playback state: {}",
                                previousState == PlayerState::MixPlaying ? "MixPlaying" : "MixPaused");
                    // State is already correct, no need to change it
                }
                else
                {
                    spdlog::warn("=== GAIN CHANGE === Changing state to SilenceMixLoaded");
                    changeState(PlayerState::SilenceMixLoaded);
                }

                spdlog::info("[PlaybackController] Mix loaded successfully");
                return true;
            }
            else
            {
                m_currentMixLoader = nullptr;
                changeState(PlayerState::Silence);
                spdlog::error("=== GAIN CHANGE === m_mixPlaybackEngine->loadMix() FAILED");
                return false;
            }
        }

        void PlaybackController::playMixFrom(double absoluteTimelineSeconds)
        {
            spdlog::debug("JUCYAUDIO: PlaybackController::playMixFrom -> Entry, position: {}", absoluteTimelineSeconds);
            if (m_currentState != PlayerState::SilenceMixLoaded && 
                m_currentState != PlayerState::MixPaused &&
                m_currentState != PlayerState::MixPlaying)
            {
                spdlog::warn("[PlaybackController] Cannot play mix from state {}", static_cast<int>(m_currentState));
                spdlog::debug("JUCYAUDIO: PlaybackController::playMixFrom -> Exit (wrong state)");
                return;
            }
            
            if (!m_mixPlaybackEngine || !m_currentMixLoader)
            {
                spdlog::error("[PlaybackController] No mix loaded");
                spdlog::debug("JUCYAUDIO: PlaybackController::playMixFrom -> Exit (no mix loaded)");
                return;
            }
            
            // Set position and start playback
            m_mixPlaybackEngine->setPosition(std::chrono::milliseconds(static_cast<int64_t>(absoluteTimelineSeconds * 1000)));
            m_mixPlaybackEngine->setPaused(false);
            
            changeState(PlayerState::MixPlaying);
            spdlog::info("[PlaybackController] Mix playing from {} seconds", absoluteTimelineSeconds);
            spdlog::debug("JUCYAUDIO: PlaybackController::playMixFrom -> Exit");
        }

        void PlaybackController::play()
        {
            spdlog::debug("JUCYAUDIO: PlaybackController::play -> Entry, state: {}", static_cast<int>(m_currentState));
            switch (m_currentState)
            {
                case PlayerState::SilenceTrackLoaded:
                    // Start track from beginning
                    m_audioTransportSource.setPosition(0.0);
                    m_audioTransportSource.start();
                    changeState(PlayerState::TrackPlaying);
                    break;
                    
                case PlayerState::TrackPaused:
                    // Resume track from paused position
                    m_audioTransportSource.start();
                    changeState(PlayerState::TrackPlaying);
                    break;
                    
                case PlayerState::SilenceMixLoaded:
                    // Start mix from beginning
                    playMixFrom(0.0);
                    break;
                    
                case PlayerState::MixPaused:
                    // Resume mix from paused position
                    if (m_mixPlaybackEngine)
                    {
                        spdlog::debug("JUCYAUDIO: PlaybackController::play -> Resuming mix, calling setPaused(false)");
                        m_mixPlaybackEngine->setPaused(false);
                        changeState(PlayerState::MixPlaying);
                    }
                    break;
                    
                default:
                    spdlog::info("[PlaybackController] Play ignored in state {}", static_cast<int>(m_currentState));
                    break;
            }
            spdlog::debug("JUCYAUDIO: PlaybackController::play -> Exit");
        }

        void PlaybackController::pause()
        {
            spdlog::debug("JUCYAUDIO: PlaybackController::pause -> Entry, state: {}", static_cast<int>(m_currentState));
            switch (m_currentState)
            {
                case PlayerState::TrackPlaying:
                    // Pause track and preserve position
                    m_pausedPosition = m_audioTransportSource.getCurrentPosition();
                    m_audioTransportSource.stop();
                    changeState(PlayerState::TrackPaused);
                    spdlog::info("[PlaybackController] Track paused at {} seconds", m_pausedPosition);
                    break;
                    
                case PlayerState::MixPlaying:
                    // Pause mix and preserve position
                    if (m_mixPlaybackEngine)
                    {
                        m_pausedPosition = m_mixPlaybackEngine->getPosition().count() / 1000.0;
                        spdlog::debug("JUCYAUDIO: PlaybackController::pause -> Pausing mix, calling setPaused(true)");
                        m_mixPlaybackEngine->setPaused(true);
                        changeState(PlayerState::MixPaused);
                        spdlog::info("[PlaybackController] Mix paused at {} seconds", m_pausedPosition);
                    }
                    break;
                    
                default:
                    spdlog::info("[PlaybackController] Pause ignored in state {}", static_cast<int>(m_currentState));
                    break;
            }
            spdlog::debug("JUCYAUDIO: PlaybackController::pause -> Exit");
        }

        void PlaybackController::stop()
        {
            spdlog::debug("JUCYAUDIO: PlaybackController::stop -> Entry, state: {}", static_cast<int>(m_currentState));
            switch (m_currentState)
            {
                case PlayerState::TrackPlaying:
                case PlayerState::TrackPaused:
                    // Stop track and reset position
                    m_audioTransportSource.stop();
                    m_audioTransportSource.setPosition(0.0);
                    m_pausedPosition = 0.0;
                    changeState(PlayerState::SilenceTrackLoaded);
                    break;
                    
                case PlayerState::MixPlaying:
                case PlayerState::MixPaused:
                    // Stop mix and reset position
                    if (m_mixPlaybackEngine)
                    {
                        spdlog::debug("JUCYAUDIO: PlaybackController::stop -> Stopping mix, calling setPaused(true)");
                        m_mixPlaybackEngine->setPaused(true);
                        m_mixPlaybackEngine->setPosition(std::chrono::milliseconds(0));
                        m_pausedPosition = 0.0;
                        changeState(PlayerState::SilenceMixLoaded);
                    }
                    break;
                    
                default:
                    spdlog::info("[PlaybackController] Stop ignored in state {}", static_cast<int>(m_currentState));
                    break;
            }
            spdlog::debug("JUCYAUDIO: PlaybackController::stop -> Exit");
        }

        void PlaybackController::seek(double positionSeconds)
        {
            switch (m_currentState)
            {
                case PlayerState::TrackPlaying:
                case PlayerState::TrackPaused:
                case PlayerState::SilenceTrackLoaded:
                    m_audioTransportSource.setPosition(positionSeconds);
                    break;
                    
                case PlayerState::MixPlaying:
                case PlayerState::MixPaused:
                case PlayerState::SilenceMixLoaded:
                    if (m_mixPlaybackEngine)
                    {
                        m_mixPlaybackEngine->setPosition(std::chrono::milliseconds(static_cast<int64_t>(positionSeconds * 1000)));
                    }
                    break;
                    
                default:
                    break;
            }
        }

        void PlaybackController::setGain(float newGain)
        {
            m_audioTransportSource.setGain(newGain);
            
            if (m_mixPlaybackEngine)
            {
                m_mixPlaybackEngine->setGain(newGain);
            }
        }
        
        void PlaybackController::setRepeatMode(RepeatMode mode)
        {
            m_playlist.repeatMode = mode;
            const char* modeStr = (mode == RepeatMode::None) ? "None"
                                : (mode == RepeatMode::One) ? "One"
                                : "All";
            spdlog::info("[PlaybackController] Repeat mode set to: {}", modeStr);

            // Save to config
            config::theSettings.audioSettings.repeatMode.set(static_cast<int>(mode));
            config::TomlBackend backend{g_strConfigFilename};
            config::theSettings.save(backend);
        }

        void PlaybackController::setShuffleMode(bool enabled)
        {
            if (m_playlist.shuffleEnabled == enabled)
                return;

            m_playlist.shuffleEnabled = enabled;
            spdlog::info("[PlaybackController] Shuffle mode {}", enabled ? "enabled" : "disabled");

            // Save to config
            config::theSettings.audioSettings.shuffleMode.set(enabled);
            config::TomlBackend backend{g_strConfigFilename};
            config::theSettings.save(backend);

            if (enabled && !m_playlist.isEmpty())
            {
                generateShuffleOrder();
            }
        }

        bool PlaybackController::isPlaying() const
        {
            return m_currentState == PlayerState::TrackPlaying || m_currentState == PlayerState::MixPlaying;
        }

        bool PlaybackController::isMixMode() const
        {
            return m_currentState == PlayerState::SilenceMixLoaded || 
                   m_currentState == PlayerState::MixPlaying || 
                   m_currentState == PlayerState::MixPaused;
        }

        bool PlaybackController::isEffectivelyPlaying() const
        {
            return isPlaying();
        }

        double PlaybackController::getCurrentPositionSeconds() const
        {
            switch (m_currentState)
            {
                case PlayerState::TrackPlaying:
                    return m_audioTransportSource.getCurrentPosition();
                    
                case PlayerState::TrackPaused:
                case PlayerState::MixPaused:
                    return m_pausedPosition;
                    
                case PlayerState::MixPlaying:
                    if (m_mixPlaybackEngine)
                    {
                        return m_mixPlaybackEngine->getPosition().count() / 1000.0;
                    }
                    break;
                    
                default:
                    break;
            }
            
            return 0.0;
        }

        double PlaybackController::getLengthInSeconds() const
        {
            switch (m_currentState)
            {
                case PlayerState::TrackPlaying:
                case PlayerState::TrackPaused:
                case PlayerState::SilenceTrackLoaded:
                    if (m_currentAudioFileSource != nullptr)
                    {
                        return m_audioTransportSource.getLengthInSeconds();
                    }
                    break;
                    
                case PlayerState::MixPlaying:
                case PlayerState::MixPaused:
                case PlayerState::SilenceMixLoaded:
                    if (m_mixPlaybackEngine)
                    {
                        return m_mixPlaybackEngine->getTotalDuration().count() / 1000.0;
                    }
                    break;
                    
                default:
                    break;
            }
            
            return 0.0;
        }

        void PlaybackController::unloadAudioSource()
        {
            m_audioTransportSource.stop();
            m_audioTransportSource.setSource(nullptr);
            m_currentAudioFileSource.reset();
            m_currentFile = juce::File();
            m_pausedPosition = 0.0;
        }

        void PlaybackController::unloadMix()
        {
            if (m_mixPlaybackEngine)
            {
                m_mixPlaybackEngine->setPaused(true);
                m_mixPlaybackEngine->unloadMix();
            }
            m_currentMixLoader = nullptr;
            m_pausedPosition = 0.0;
        }

        void PlaybackController::processAudioBlock(const juce::AudioBuffer<float>& buffer)
        {
            if (buffer.getNumChannels() > 0)
            {
                m_peakLeft = buffer.getMagnitude(0, 0, buffer.getNumSamples());
            }
            else
            {
                m_peakLeft = 0.0f;
            }

            if (buffer.getNumChannels() > 1)
            {
                m_peakRight = buffer.getMagnitude(1, 0, buffer.getNumSamples());
            }
            else
            {
                // For mono, copy left to right
                m_peakRight = m_peakLeft.load();
            }
        }

        void PlaybackController::updateMasterEQ(const audio::model::EQSettings& settings)
        {
            m_currentEQSettings = settings;
            m_masterEqualizer.updateParameters(settings);
            
            // If we're in mix mode, also update the mix project loader
            if (m_currentMixLoader)
            {
                m_currentMixLoader->setMasterEQSettings(settings);
            }
        }
        
        void PlaybackController::updateMasterReverb(const audio::model::ReverbSettings& settings)
        {
            m_currentReverbSettings = settings;
            m_masterReverb.updateParameters(settings);

            // If we're in mix mode, also update the mix project loader
            if (m_currentMixLoader)
            {
                // TODO: Add setMasterReverbSettings to MixProjectLoader when ready
                // m_currentMixLoader->setMasterReverbSettings(settings);
            }
        }

        void PlaybackController::setVisualizerFIFO(audio::AudioVisualizerFIFO* fifo)
        {
            m_visualizerFIFO = fifo;
            spdlog::info("[PlaybackController] Visualizer FIFO connected (unified tap point)");
        }

        // --- Playlist Management Implementation ---

        void PlaybackController::setPlaylist(const std::vector<database::TrackInfo>& tracks,
                                             size_t startIndex,
                                             PlaylistSource source,
                                             const std::string& sourcePath)
        {
            if (tracks.empty())
            {
                spdlog::warn("[PlaybackController] setPlaylist called with empty track list");
                return;
            }

            // Stop any mix playback
            if (isMixMode())
            {
                unloadMix();
            }

            // Set up the playlist
            m_playlist.tracks = tracks;
            m_playlist.currentIndex = std::min(startIndex, tracks.size() - 1);
            m_playlist.source = source;
            m_playlist.sourcePath = sourcePath;

            // Generate shuffle order if shuffle is enabled
            if (m_playlist.shuffleEnabled)
            {
                generateShuffleOrder();
            }

            spdlog::info("[PlaybackController] Playlist set with {} tracks, starting at index {}",
                        tracks.size(), m_playlist.currentIndex);

            // Clear single track info since we're now in playlist mode
            m_singleTrackInfo.reset();

            // Start playing the first track
            playCurrentPlaylistTrack();
        }

        void PlaybackController::clearPlaylist()
        {
            m_playlist.clear();
            m_singleTrackInfo.reset();
            spdlog::info("[PlaybackController] Playlist cleared");
        }

        bool PlaybackController::nextTrack()
        {
            // In mix mode, nextTrack seeks to next track boundary
            if (isMixMode())
            {
                // TODO: Implement mix track navigation
                spdlog::info("[PlaybackController] nextTrack in mix mode - not yet implemented");
                return false;
            }

            // If no playlist, nothing to do
            if (m_playlist.isEmpty())
            {
                spdlog::debug("[PlaybackController] nextTrack called but no playlist");
                return false;
            }

            // Handle repeat-one: restart current track
            if (m_playlist.repeatMode == RepeatMode::One)
            {
                seek(0.0);
                if (m_currentState == PlayerState::TrackPaused || m_currentState == PlayerState::SilenceTrackLoaded)
                {
                    play();
                }
                return true;
            }

            // Calculate next index
            size_t nextIndex = m_playlist.currentIndex + 1;

            // Check if we've reached the end
            if (nextIndex >= m_playlist.size())
            {
                if (m_playlist.repeatMode == RepeatMode::All)
                {
                    // Wrap to beginning
                    nextIndex = 0;
                    if (m_playlist.shuffleEnabled)
                    {
                        // Reshuffle for next iteration
                        generateShuffleOrder();
                    }
                }
                else
                {
                    // No repeat - stop at end
                    spdlog::info("[PlaybackController] Reached end of playlist");
                    stop();
                    return false;
                }
            }

            m_playlist.currentIndex = nextIndex;
            return playCurrentPlaylistTrack();
        }

        bool PlaybackController::previousTrack()
        {
            // In mix mode, previousTrack seeks to previous track boundary
            if (isMixMode())
            {
                // TODO: Implement mix track navigation
                spdlog::info("[PlaybackController] previousTrack in mix mode - not yet implemented");
                return false;
            }

            // If no playlist, nothing to do
            if (m_playlist.isEmpty())
            {
                spdlog::debug("[PlaybackController] previousTrack called but no playlist");
                return false;
            }

            // Standard behavior: if >3 seconds into track, restart; otherwise go to previous
            const double currentPos = getCurrentPositionSeconds();
            if (currentPos > 3.0)
            {
                seek(0.0);
                return true;
            }

            // Go to previous track
            if (m_playlist.currentIndex > 0)
            {
                m_playlist.currentIndex--;
                return playCurrentPlaylistTrack();
            }
            else if (m_playlist.repeatMode == RepeatMode::All)
            {
                // Wrap to end
                m_playlist.currentIndex = m_playlist.size() - 1;
                return playCurrentPlaylistTrack();
            }
            else
            {
                // At beginning, just restart current track
                seek(0.0);
                return true;
            }
        }

        void PlaybackController::handleTrackEnded()
        {
            spdlog::debug("[PlaybackController] handleTrackEnded called");

            // If we have a playlist, auto-advance
            if (!m_playlist.isEmpty())
            {
                nextTrack();
            }
            else if (m_singleTrackInfo.has_value())
            {
                // Single track mode - just stop (or handle repeat-one if needed)
                changeState(PlayerState::SilenceTrackLoaded);
            }
        }

        const database::TrackInfo* PlaybackController::getCurrentTrackInfo() const
        {
            // First check playlist
            if (!m_playlist.isEmpty())
            {
                return m_playlist.getCurrentTrack();
            }

            // Then check single track
            if (m_singleTrackInfo.has_value())
            {
                return &m_singleTrackInfo.value();
            }

            return nullptr;
        }

        TrackId PlaybackController::getCurrentTrackId() const
        {
            const auto* trackInfo = getCurrentTrackInfo();
            return trackInfo ? trackInfo->trackId : -1;
        }

        void PlaybackController::generateShuffleOrder()
        {
            const size_t count = m_playlist.size();
            m_playlist.shuffleOrder.resize(count);

            // Fill with sequential indices
            for (size_t i = 0; i < count; ++i)
            {
                m_playlist.shuffleOrder[i] = i;
            }

            // Shuffle using Fisher-Yates
            for (size_t i = count - 1; i > 0; --i)
            {
                std::uniform_int_distribution<size_t> dist(0, i);
                size_t j = dist(m_randomEngine);
                std::swap(m_playlist.shuffleOrder[i], m_playlist.shuffleOrder[j]);
            }

            // Move current track to front if we're mid-playlist
            if (m_playlist.currentIndex > 0 && m_playlist.currentIndex < count)
            {
                // Find where current track ended up in shuffle and swap to front
                for (size_t i = 0; i < count; ++i)
                {
                    if (m_playlist.shuffleOrder[i] == m_playlist.currentIndex)
                    {
                        std::swap(m_playlist.shuffleOrder[0], m_playlist.shuffleOrder[i]);
                        break;
                    }
                }
                m_playlist.currentIndex = 0;
            }

            spdlog::debug("[PlaybackController] Generated shuffle order for {} tracks", count);
        }

        bool PlaybackController::playCurrentPlaylistTrack()
        {
            const auto* trackInfo = m_playlist.getCurrentTrack();
            if (!trackInfo)
            {
                spdlog::error("[PlaybackController] No current track in playlist");
                return false;
            }

            // Reconstruct the file path and load
            const auto filePath = trackInfo->reconstructFullPath();
            juce::File audioFile(juce::String(filePath.string()));

            spdlog::info("[PlaybackController] Playing playlist track {}/{}: {}",
                        m_playlist.currentIndex + 1, m_playlist.size(),
                        trackInfo->filename);

            const bool success = loadAndPlayFile(audioFile);

            if (success)
            {
                // Update play count and last_played timestamp
                database::theTrackLibrary.getTrackDatabase().incrementTrackPlayCount(trackInfo->trackId);
                notifyTrackChanged();
            }

            return success;
        }

        void PlaybackController::notifyTrackChanged()
        {
            if (onCurrentTrackChanged)
            {
                const auto trackId = getCurrentTrackId();
                const size_t index = m_playlist.isEmpty() ? 0 : m_playlist.currentIndex;
                onCurrentTrackChanged(trackId, index);
            }
        }

    } // namespace ui
} // namespace jucyaudio
