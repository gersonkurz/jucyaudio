#include <Audio/MixPlaybackEngine.h>
#include <UI/PlaybackController.h>
#include <UI/PlaybackToolbarComponent.h>

namespace jucyaudio
{
    namespace ui
    {
        PlaybackController::PlaybackController(PlaybackToolbarComponent &toolbar)
            : m_playbackToolbar{toolbar},
              m_mixPlaybackEngine{std::make_unique<audio::MixPlaybackEngine>()}
        {
            m_audioFormatManager.registerBasicFormats();

            m_playbackToolbar.onPlayClicked = [this]
            {
                spdlog::info("[PlaybackController] Play button clicked - callback triggered");
                play();  // Start single track playback
            };
            m_playbackToolbar.onPauseClicked = [this]
            {
                spdlog::info("[PlaybackController] Pause button clicked - callback triggered");
                pause();
            };
            m_playbackToolbar.onStopClicked = [this]
            {
                spdlog::info("[PlaybackController] Stop button clicked - callback triggered");
                stop();
            };
            m_playbackToolbar.onPositionSeek = [this](double newPos)
            {
                seek(newPos);
            };
            m_playbackToolbar.onVolumeChanged = [this](float newGain)
            {
                setGain(newGain);
            };
        }

        PlaybackController::~PlaybackController()
        {
            releaseResources(); // Ensure everything is cleaned up
        }

        void PlaybackController::syncUIToPlaybackControllerState(bool hasRowSelected)
        {
            PlaybackController::State controllerState = getCurrentState();
            bool isEffectivelyPlaying = (controllerState == PlaybackController::State::Playing || controllerState == PlaybackController::State::Starting);
            
            // Check if mix is playing
            bool mixIsPlaying = isMixPlaying ? isMixPlaying() : false;
            
            spdlog::info("[PlaybackController] syncUIToPlaybackControllerState: state={}, isEffectivelyPlaying={}, mixIsPlaying={}, hasRowSelected={}, currentFile='{}'",
                        static_cast<int>(controllerState), isEffectivelyPlaying, mixIsPlaying, hasRowSelected, getCurrentFilepath().toStdString());

            m_playbackToolbar.setIsPlaying(isEffectivelyPlaying || mixIsPlaying);

            bool canCurrentlyPlay = !getCurrentFilepath().isEmpty() || hasRowSelected;
            bool canCurrentlyStopOrPause = (!getCurrentFilepath().isEmpty() && controllerState != PlaybackController::State::Stopped) || mixIsPlaying;
            
            spdlog::info("[PlaybackController] Button states: canPlay={}, canStopOrPause={}", canCurrentlyPlay, canCurrentlyStopOrPause);

            m_playbackToolbar.setPlayButtonEnabled(canCurrentlyPlay);
            m_playbackToolbar.setStopButtonEnabled(canCurrentlyStopOrPause);

            if (getCurrentFilepath().isEmpty())
            {
                m_playbackToolbar.setPositionSliderRange(1.0);
                m_playbackToolbar.updateTimeDisplay(0.0, 0.0);
                m_playbackToolbar.setPositionSliderValue(0.0);
            }
            else
            {
                double totalLen = getLengthInSeconds();
                m_playbackToolbar.setPositionSliderRange(totalLen > 0.0 ? totalLen : 1.0); // Avoid zero range
                m_playbackToolbar.updateTimeDisplay(getCurrentPositionSeconds(), totalLen);
                m_playbackToolbar.setPositionSliderValue(getCurrentPositionSeconds());
            }
            m_playbackToolbar.setVolumeSliderValue(getTransportSource().getGain());
            if (getTransportSource().hasStreamFinished() && getCurrentState() == PlaybackController::State::Playing)
            {
                spdlog::info("Track finished playing (MainComponent ChangeListener).");
                stop(); // This will trigger another state change -> sync
            }
        }

        void PlaybackController::onTimerEvent()
        {
            const auto controllerState = getCurrentState();

            if (controllerState == State::Playing || controllerState == State::Paused)
            {
                // Check if a file is actually loaded in the controller,
                // not just relying on its state, as state might be Paused but file was just unloaded.
                // getCurrentFilepath().isEmpty() is a good check.
                if (!getCurrentFilepath().isEmpty())
                {
                    if (!m_playbackToolbar.isPositionSliderDragging()) // <<<< CHECK THE FLAG
                    {
                        double currentPos = getCurrentPositionSeconds();
                        double totalLength = getLengthInSeconds();
                        m_playbackToolbar.updateTimeDisplay(currentPos, totalLength);
                        m_playbackToolbar.setPositionSliderValue(currentPos);
                    }
                    else
                    {
                        double currentPos = getCurrentPositionSeconds();
                        double totalLen = getLengthInSeconds();

                        m_playbackToolbar.updateTimeDisplay(currentPos, totalLen);
                        // setPositionSliderValue in PlaybackToolbarComponent already checks if user is dragging
                        m_playbackToolbar.setPositionSliderValue(currentPos);
                    }
                }
                else
                {
                    // State might be Paused/Playing conceptually, but no file, so reset UI.
                    // This case should ideally be covered by syncUIToPlaybackControllerState when a file is unloaded.
                    m_playbackToolbar.updateTimeDisplay(0.0, 0.0);
                    m_playbackToolbar.setPositionSliderValue(0.0);
                }
            }
            else if (controllerState == State::Stopped)
            {
                // If stopped, ensure UI reflects this (especially if a file *was* loaded)
                if (!m_playbackToolbar.isMouseButtonDownAnywhere() && /* safety: don't fight user input */
                    !getCurrentFilepath().isEmpty())
                {
                    // If a file was loaded but now we are stopped, show 0 / total
                    m_playbackToolbar.updateTimeDisplay(0.0, getLengthInSeconds());
                    m_playbackToolbar.setPositionSliderValue(0.0);
                }
                else if (getCurrentFilepath().isEmpty())
                {
                    // No file loaded, UI should show 0 / 0
                    m_playbackToolbar.updateTimeDisplay(0.0, 0.0);
                    m_playbackToolbar.setPositionSliderValue(0.0);
                }
                // If user manually set position slider while stopped, this won't override it, which is good.
            }
        }

        void PlaybackController::changeState(State newState)
        {
            if (m_currentState == newState)
                return;

            // A simple guard against rapid/re-entrant state changes if needed,
            // though direct calls from MainComponent should be okay.
            // juce::ScopedValueSetter<std::atomic<bool>> stateGuard(m_isCurrentlyChangingState, true, false);
            // if (!m_isCurrentlyChangingState) return; // Or handle re-entrancy more gracefully

            m_currentState = newState;
            spdlog::debug("PlaybackController state changed to: {}", static_cast<int>(m_currentState));

            sendChangeMessage(); // Notify listeners (e.g., MainComponent) that our *controller's* state changed
                                 // This is different from listening to m_audioTransportSource directly for its events.
                                 // MainComponent can use this to update its own m_playbackState enum if it mirrors this.
        }

        void PlaybackController::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
        {
            this->m_deviceSampleRate = sampleRate;
            this->m_deviceBlockSize = samplesPerBlockExpected;
            m_isDevicePrepared = true; // Mark that we have valid device settings

            // Also prepare the transport source if it already has a source (e.g., if app starts with a track preloaded)
            // Or, this call could just be to get the device settings, and individual file loads re-prepare.
            // For now, just store them. The transport source will be prepared when a file is loaded.
            spdlog::info("PlaybackController::prepareToPlay - Device SR: {}, BlockSize: {}", m_deviceSampleRate, m_deviceBlockSize);
            m_audioTransportSource.prepareToPlay(samplesPerBlockExpected, sampleRate);
            // Prepare mix playback engine
            if (m_mixPlaybackEngine)
            {
                m_mixPlaybackEngine->prepareToPlay(samplesPerBlockExpected, sampleRate);
            }
        }

        void PlaybackController::getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill)
        {
            if (m_currentState == State::Stopped || m_currentState == State::Paused)
            {
                bufferToFill.clearActiveBufferRegion();
                return;
            }

            if (m_playbackMode == PlaybackMode::MixPreview && m_mixPlaybackEngine && m_mixPlaybackEngine->isMixLoaded())
            {
                // Use mix playback engine
                m_mixPlaybackEngine->getNextAudioBlock(bufferToFill);
            }
            else if (m_currentAudioFileSource != nullptr)
            {
                // Use single track playback
                m_audioTransportSource.getNextAudioBlock(bufferToFill);
            }
            else
            {
                bufferToFill.clearActiveBufferRegion();
            }
        }

        void PlaybackController::releaseResources()
        {
            stop(); // Ensure playback is stopped
            m_audioTransportSource.releaseResources();
        }

        void PlaybackController::unloadAudioSource()
        {
            m_audioTransportSource.setSource(nullptr); // Releases previous source
            m_currentAudioFileSource.reset();          // Deletes the AudioFormatReaderSource (and its reader)
            m_currentFile = juce::File{};
        }

        void PlaybackController::loadFileInternal(const juce::File &audioFile)
        {
            unloadAudioSource(); // Clear any existing source

            juce::AudioFormatReader *reader = m_audioFormatManager.createReaderFor(audioFile);

            if (reader != nullptr)
            {
                spdlog::info(
                    "Reader created: Sample Rate={}, Num Channels={}, Length in Samples={}", reader->sampleRate, reader->numChannels, reader->lengthInSamples);
                if (reader->sampleRate <= 0 || reader->numChannels <= 0)
                {
                    spdlog::error("Reader created with invalid properties!");
                    // Handle error, don't proceed to setSource
                    m_currentFile = juce::File{};
                    delete reader; // If AudioFormatReaderSource doesn't take ownership on failure
                    return;        // Or throw
                }
                m_currentAudioFileSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true); // true = owns reader
                m_audioTransportSource.setSource(m_currentAudioFileSource.get(),
                    0,       // readAheadBuffer (0 for default)
                    nullptr, // sourceSampleRateToCorrectFor
                    reader->sampleRate);
                m_currentFile = audioFile;
                spdlog::info("PlaybackController: Loaded file '{}', duration: {:.2f}s", m_currentFile.getFullPathName().toStdString(), getLengthInSeconds());
            }
            else
            {
                m_currentFile = juce::File{}; // Clear current file on load failure
                spdlog::error("PlaybackController: Could not create reader for: {}", audioFile.getFullPathName().toStdString());
                // Optionally: throw an exception or set an error state that can be queried.
            }
        }

        bool PlaybackController::loadAndPlayFile(const juce::File &audioFile)
        {
            if (!audioFile.existsAsFile())
            {
                spdlog::error("PlaybackController: File does not exist: {}", audioFile.getFullPathName().toStdString());
                changeState(State::Stopped); // Ensure state is consistent
                return false;
            }

            // Stop any current playback fully before loading new
            if (m_currentState != State::Stopped && m_currentState != State::Stopping)
            { // If it's not already stopping/stopped
                changeState(State::Stopping);
                m_audioTransportSource.stop();
            }
            // Unload previous source explicitly before loading new.
            // loadFileInternal will also call unloadAudioSource, but doing it here is clearer.
            unloadAudioSource();

            changeState(State::Starting); // Indicate we are attempting to load and start
            loadFileInternal(audioFile);  // This sets m_currentAudioFileSource and calls m_audioTransportSource.setSource()

            if (m_currentAudioFileSource != nullptr)
            {
                if (m_isDevicePrepared && m_deviceSampleRate > 0 && m_deviceBlockSize > 0)
                {
                    // Device has been prepared by the host (MainComponent's AudioAppComponent callback),
                    // so we have valid sampleRate and blockSize.
                    // Now, prepare the transport source WITH THE NEWLY LOADED AUDIO SOURCE,
                    // using the CURRENT DEVICE SETTINGS.
                    m_audioTransportSource.prepareToPlay(m_deviceBlockSize, m_deviceSampleRate);
                    spdlog::info(
                        "PlaybackController: Transport source prepared for new file with device SR: {}, BlockSize: {}", m_deviceSampleRate, m_deviceBlockSize);
                }
                else
                {
                    // This case should be rare if MainComponent::prepareToPlay is called before any playback attempt.
                    // It means either the audio device isn't ready, or PlaybackController's prepareToPlay wasn't called.
                    spdlog::error(
                        "PlaybackController: Audio device not prepared (sampleRate/blockSize unknown). Cannot prepare transport source for playback. File: {}",
                        audioFile.getFullPathName().toStdString());
                    changeState(State::Stopped);
                    return false; // Cannot safely start playback
                }

                m_audioTransportSource.setPosition(0.0); // Always start new files from the beginning
                m_audioTransportSource.start();

                if (m_audioTransportSource.isPlaying())
                {
                    changeState(State::Playing); // Update our controller's state
                }
                else
                {
                    // Start might fail if source is bad or prepareToPlay had issues
                    spdlog::warn(
                        "PlaybackController: m_audioTransportSource.start() did not result in playing state for {}", audioFile.getFullPathName().toStdString());
                    changeState(State::Stopped);
                    return false;
                }
                return true;
            }
            else
            {
                // loadFileInternal failed to create a reader source
                changeState(State::Stopped);
                return false;
            }
        }

        bool PlaybackController::loadAndPlayFileFromPosition(const juce::File &audioFile, double startPositionSeconds)
        {
            if (!loadAndPlayFile(audioFile))
                return false;

            // Seek to the desired position after loading
            seek(startPositionSeconds);
            return true;
        }

        bool PlaybackController::loadAndPlayMix(audio::MixProjectLoader *mixLoader, double startPositionSeconds)
        {
            if (!mixLoader || !m_mixPlaybackEngine)
            {
                spdlog::error("PlaybackController::loadAndPlayMix - Invalid mix loader or engine");
                return false;
            }

            // Stop any current playback
            stop();

            // Switch to mix preview mode
            setPlaybackMode(PlaybackMode::MixPreview);

            // Load the mix
            if (!m_mixPlaybackEngine->loadMix(mixLoader))
            {
                spdlog::error("PlaybackController::loadAndPlayMix - Failed to load mix");
                return false;
            }

            // If device is prepared, prepare the mix engine
            if (m_isDevicePrepared)
            {
                m_mixPlaybackEngine->prepareToPlay(m_deviceBlockSize, m_deviceSampleRate);
            }

            // Set initial position
            if (startPositionSeconds > 0.0)
            {
                m_mixPlaybackEngine->setPosition(Duration_t{static_cast<int64_t>(startPositionSeconds * 1000.0)});
            }

            // Start playback
            changeState(State::Playing);

            spdlog::info("PlaybackController::loadAndPlayMix - Mix loaded and playing from {}s", startPositionSeconds);
            return true;
        }

        void PlaybackController::setPlaybackMode(PlaybackMode mode)
        {
            if (m_playbackMode == mode)
                return;

            // Stop current playback when switching modes
            stop();

            m_playbackMode = mode;
            spdlog::info("PlaybackController: Switched to {} mode", mode == PlaybackMode::SingleTrack ? "SingleTrack" : "MixPreview");
        }

        void PlaybackController::play()
        {
            if (m_currentAudioFileSource == nullptr)
            {
                spdlog::warn("PlaybackController::play() called but no file loaded.");
                return;
            }
            if (!m_audioTransportSource.isPlaying())
            {
                changeState(State::Starting); // Indicate intent
                m_audioTransportSource.start();
                // If start() was successful, the transport will be playing.
                if (m_audioTransportSource.isPlaying())
                {
                    changeState(State::Playing);
                }
            }
        }

        void PlaybackController::pause()
        {
            // Stop mix playback if it's playing
            if (onStopMixPlayback)
            {
                onStopMixPlayback();
            }
            
            if (m_audioTransportSource.isPlaying())
            {
                changeState(State::Pausing);
                m_audioTransportSource.stop(); // stop() effectively pauses if source is still set
                changeState(State::Paused);
            }
        }

        void PlaybackController::stop()
        {
            // Stop mix playback if it's playing
            if (onStopMixPlayback)
            {
                onStopMixPlayback();
            }
            
            if (m_currentState != State::Stopped)
            {
                changeState(State::Stopping);
                m_audioTransportSource.stop();
                m_audioTransportSource.setPosition(0.0); // Rewind on stop
                // Optionally unload source completely on stop:
                // unloadAudioSource();
                // For now, stop means "ready to play from beginning if play() is called next"
                changeState(State::Stopped);
            }
        }
        
        void PlaybackController::stopSingleTrackOnly()
        {
            spdlog::info("[PlaybackController] stopSingleTrackOnly called, current state: {}", static_cast<int>(m_currentState));
            // Stop only single track playback without triggering mix stop callback
            if (m_currentState != State::Stopped)
            {
                changeState(State::Stopping);
                m_audioTransportSource.stop();
                m_audioTransportSource.setPosition(0.0); // Rewind on stop
                changeState(State::Stopped);
                spdlog::info("[PlaybackController] Single track stopped");
            }
            else
            {
                spdlog::info("[PlaybackController] Already stopped, nothing to do");
            }
        }

        void PlaybackController::togglePlayPause()
        {
            if (isPlaying())
            { // isPlaying() should check m_audioTransportSource.isPlaying()
                pause();
            }
            else
            {
                play(); // play() will start from current position, or from beginning if just stopped
            }
        }

        void PlaybackController::seek(double positionSeconds)
        {
            if (m_playbackMode == PlaybackMode::MixPreview && m_mixPlaybackEngine && m_mixPlaybackEngine->isMixLoaded())
            {
                m_mixPlaybackEngine->setPosition(Duration_t{static_cast<int64_t>(positionSeconds * 1000.0)});
            }
            else if (m_currentAudioFileSource != nullptr)
            {
                m_audioTransportSource.setPosition(positionSeconds);
            }
        }

        void PlaybackController::setGain(float newGain)
        {
            m_audioTransportSource.setGain(newGain);
        }

        bool PlaybackController::isPlaying() const
        {
            return m_audioTransportSource.isPlaying();
        }

        double PlaybackController::getCurrentPositionSeconds() const
        {
            if (m_playbackMode == PlaybackMode::MixPreview && m_mixPlaybackEngine && m_mixPlaybackEngine->isMixLoaded())
            {
                return m_mixPlaybackEngine->getPosition().count() / 1000.0;
            }
            return m_audioTransportSource.getCurrentPosition();
        }

        double PlaybackController::getLengthInSeconds() const
        {
            if (m_playbackMode == PlaybackMode::MixPreview && m_mixPlaybackEngine && m_mixPlaybackEngine->isMixLoaded())
            {
                return m_mixPlaybackEngine->getTotalDuration().count() / 1000.0;
            }
            return m_audioTransportSource.getLengthInSeconds();
        }
    } // namespace ui
} // namespace jucyaudio
