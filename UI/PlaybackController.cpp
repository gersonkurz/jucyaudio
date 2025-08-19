#include <UI/PlaybackController.h>
#include <Audio/MixPlaybackEngine.h>
#include <Audio/MixProjectLoader.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        using namespace audio;

        PlaybackController::PlaybackController()
        {
            m_audioFormatManager.registerBasicFormats();
            
            // Initialize mix playback engine
            m_mixPlaybackEngine = std::make_unique<MixPlaybackEngine>();
            
            spdlog::info("[PlaybackController] Initialized in Silence state");
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
            
            // Apply master EQ and reverb to the audio (after getting the source audio)
            if (m_currentState == PlayerState::TrackPlaying || m_currentState == PlayerState::MixPlaying)
            {
                juce::dsp::AudioBlock<float> block(*bufferToFill.buffer, 
                                                  static_cast<size_t>(bufferToFill.startSample));
                m_masterEqualizer.process(block);
                m_masterReverb.process(block);
            }
        }

        void PlaybackController::releaseResources()
        {
            m_audioTransportSource.releaseResources();
            m_masterEqualizer.reset();
            m_masterReverb.reset();
            
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
            spdlog::debug("JUCYAUDIO: PlaybackController::loadMix -> Entry");
            if (!mixLoader)
            {
                spdlog::error("[PlaybackController] Null mix loader provided");
                spdlog::debug("JUCYAUDIO: PlaybackController::loadMix -> Exit (failure, null loader)");
                return false;
            }
            
            // Stop any track playback
            if (m_currentState == PlayerState::TrackPlaying || m_currentState == PlayerState::TrackPaused)
            {
                unloadAudioSource();
            }
            
            // Load the mix
            m_currentMixLoader = mixLoader;
            if (m_mixPlaybackEngine->loadMix(mixLoader))
            {
                // Apply the mix's EQ settings
                m_masterEqualizer.updateParameters(mixLoader->getMasterEQSettings());
                
                changeState(PlayerState::SilenceMixLoaded);
                spdlog::info("[PlaybackController] Mix loaded successfully");
                spdlog::debug("JUCYAUDIO: PlaybackController::loadMix -> Exit (success)");
                return true;
            }
            else
            {
                m_currentMixLoader = nullptr;
                changeState(PlayerState::Silence);
                spdlog::error("[PlaybackController] Failed to load mix");
                spdlog::debug("JUCYAUDIO: PlaybackController::loadMix -> Exit (failure, engine load failed)");
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
        
        void PlaybackController::setRepeatMode(bool enabled)
        {
            m_repeatMode = enabled;
            spdlog::info("PlaybackController: Repeat mode {}", enabled ? "enabled" : "disabled");
            // Repeat mode will be used by MainComponent for playlist/folder repeat
        }
        
        void PlaybackController::setShuffleMode(bool enabled)
        {
            m_shuffleMode = enabled;
            spdlog::info("PlaybackController: Shuffle mode {}", enabled ? "enabled" : "disabled");
            // Shuffle mode will be used when selecting the next track
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

        void PlaybackController::withMixEngineLock(std::function<void()> action)
        {
            if (m_mixPlaybackEngine)
            {
                const juce::ScopedLock lock(m_mixPlaybackEngine->getLock());
                action();
            }
            else
            {
                // If there's no engine, there's nothing to lock.
                // It might be safer to just run the action, or log a warning.
                // For now, we'll just run it, assuming the action is safe without a mix.
                action();
            }
        }
        
        void PlaybackController::updateMasterEQ(const audio::model::EQSettings& settings)
        {
            m_masterEqualizer.updateParameters(settings);
            
            // If we're in mix mode, also update the mix project loader
            if (m_currentMixLoader)
            {
                m_currentMixLoader->setMasterEQSettings(settings);
            }
        }
        
        void PlaybackController::updateMasterReverb(const audio::model::ReverbSettings& settings)
        {
            m_masterReverb.updateParameters(settings);
            
            // If we're in mix mode, also update the mix project loader
            if (m_currentMixLoader)
            {
                // TODO: Add setMasterReverbSettings to MixProjectLoader when ready
                // m_currentMixLoader->setMasterReverbSettings(settings);
            }
        }

    } // namespace ui
} // namespace jucyaudio