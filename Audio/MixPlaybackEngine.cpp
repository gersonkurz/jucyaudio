#include <Audio/MixPlaybackEngine.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>
#include <Database/Includes/Constants.h>
#include <spdlog/spdlog.h>

#if MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE

namespace jucyaudio
{
    namespace audio
    {
        // Helper function from ExportMixImplementation
        extern float interpolateVolumeFromEnvelope(const std::vector<EnvelopePoint> &envelopePoints, Duration_t timeInTrack);

        //==============================================================================
        // PlaybackTrackSource implementation
        //==============================================================================
        PlaybackTrackSource::PlaybackTrackSource(TrackId id, const TrackInfo *ti, const MixTrack *mt)
            : trackId{id},
              trackInfo{ti},
              mixTrack{mt}
        {
        }

        bool PlaybackTrackSource::prepare(juce::AudioFormatManager &formatManager, double targetSampleRate, int blockSize)
        {
            juce::File sourceFile{ui::jucePathFromFs(trackInfo->filepath)};
            reader.reset(formatManager.createReaderFor(sourceFile));

            if (!reader)
            {
                spdlog::error("MixPlaybackEngine: Failed to create reader for track {} ({})", trackId, pathToString(trackInfo->filepath));
                return false;
            }

            readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader.get(), false);

            // Setup resampling if needed
            if (std::abs(reader->sampleRate - targetSampleRate) > 0.01)
            {
                resampler = std::make_unique<juce::ResamplingAudioSource>(readerSource.get(), false, reader->numChannels);
                resampler->setResamplingRatio(reader->sampleRate / targetSampleRate);
                resampler->prepareToPlay(blockSize, targetSampleRate);
            }
            else
            {
                readerSource->prepareToPlay(blockSize, targetSampleRate);
            }

            return true;
        }

        //==============================================================================
        // MixPlaybackEngine implementation
        //==============================================================================
        MixPlaybackEngine::MixPlaybackEngine()
        {
            m_formatManager.registerBasicFormats();
            m_isPaused = true; // Start paused
        }

        MixPlaybackEngine::~MixPlaybackEngine()
        {
            unloadMix();
        }

        bool MixPlaybackEngine::loadMix(MixProjectLoader *mixLoader)
        {
            if (!mixLoader)
            {
                spdlog::error("MixPlaybackEngine: Null mix loader provided");
                return false;
            }

            std::lock_guard<std::mutex> lock(m_mutex);

            // Clean up any existing mix (using internal version to avoid recursive lock)
            unloadMixInternal();

            m_mixLoader = mixLoader;

            // Calculate total duration
            const auto &mixTracks = m_mixLoader->getMixTracks();
            if (mixTracks.empty())
            {
                spdlog::error("MixPlaybackEngine: No tracks in mix");
                return false;
            }

            // Find the last track end time
            Duration_t maxEndTime{0};
            for (const auto &mixTrack : mixTracks)
            {
                if (const auto *trackInfo = m_mixLoader->getTrackInfoForId(mixTrack.trackId))
                {
                    Duration_t trackEndTime = mixTrack.mixStartTime + trackInfo->duration;
                    maxEndTime = std::max(maxEndTime, trackEndTime);
                }
            }

            m_totalDurationMs = maxEndTime;
            spdlog::info("MixPlaybackEngine: Loaded mix with duration {}", durationToString(m_totalDurationMs));

            // Prepare track sources if we're already prepared
            if (m_isPrepared)
            {
                return prepareTrackSources();
            }

            return true;
        }

        void MixPlaybackEngine::unloadMix()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            unloadMixInternal();
        }

        void MixPlaybackEngine::unloadMixInternal()
        {
            // Release all track sources
            for (auto &source : m_trackSources)
            {
                if (source->getAudioSource())
                {
                    source->getAudioSource()->releaseResources();
                }
            }
            m_trackSources.clear();

            m_mixLoader = nullptr;
            m_currentPositionSamples = 0;
            m_totalDurationMs = Duration_t{0};
        }

        void MixPlaybackEngine::setPosition(Duration_t positionMs)
        {
            if (!m_mixLoader)
                return;

            spdlog::info("MixPlaybackEngine::setPosition called with {} ms", positionMs.count());
            
            // Clamp position to valid range
            positionMs = std::max(Duration_t{0}, std::min(positionMs, m_totalDurationMs));

            // Convert to samples
            juce::int64 positionSamples = static_cast<juce::int64>((positionMs.count() / 1000.0) * m_sampleRate);

            std::lock_guard<std::mutex> lock(m_mutex);
            m_currentPositionSamples = positionSamples;

            // Update all track source positions
            for (auto &source : m_trackSources)
            {
                // Calculate this track's position relative to its start
                Duration_t trackStartMs = source->mixTrack->mixStartTime;
                juce::int64 trackStartSamples = static_cast<juce::int64>((trackStartMs.count() / 1000.0) * m_sampleRate);

                if (positionSamples >= trackStartSamples)
                {
                    // Position is after track start
                    juce::int64 positionInTrack = positionSamples - trackStartSamples;

                    // Convert to source sample rate
                    juce::int64 positionInSourceSamples = 0;
                    if (source->reader)
                    {
                        positionInSourceSamples = static_cast<juce::int64>(positionInTrack * source->reader->sampleRate / m_sampleRate);
                    }

                    source->currentPositionInSourceSamples = positionInSourceSamples;

                    if (source->readerSource)
                    {
                        source->readerSource->setNextReadPosition(positionInSourceSamples);
                    }
                }
                else
                {
                    // Position is before track start
                    source->currentPositionInSourceSamples = 0;
                    if (source->readerSource)
                    {
                        source->readerSource->setNextReadPosition(0);
                    }
                }
            }
        }

        Duration_t MixPlaybackEngine::getPosition() const
        {
            double positionSeconds = m_currentPositionSamples.load() / m_sampleRate;
            return Duration_t{static_cast<int64_t>(positionSeconds * 1000.0)};
        }

        void MixPlaybackEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            // Store current position before preparing
            auto currentPos = m_currentPositionSamples.load();
            
            m_sampleRate = sampleRate;
            m_blockSize = samplesPerBlockExpected;
            m_isPrepared = true;

            spdlog::info("MixPlaybackEngine: Preparing to play at {} Hz with block size {}", sampleRate, samplesPerBlockExpected);

            if (m_mixLoader)
            {
                prepareTrackSources();
                
                // Restore position after preparing track sources
                if (currentPos > 0)
                {
                    m_currentPositionSamples = currentPos;
                    spdlog::info("MixPlaybackEngine: Restored position to {} samples after prepare", currentPos);
                }
            }
        }

        void MixPlaybackEngine::releaseResources()
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            for (auto &source : m_trackSources)
            {
                if (source->getAudioSource())
                {
                    source->getAudioSource()->releaseResources();
                }
            }

            m_isPrepared = false;
        }

        void MixPlaybackEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill)
        {
            if (!m_mixLoader || !m_isPrepared || m_isPaused)
            {
                bufferToFill.clearActiveBufferRegion();
                return;
            }

            std::lock_guard<std::mutex> lock(m_mutex);

            // Clear the output buffer
            bufferToFill.clearActiveBufferRegion();

            // Get current position
            juce::int64 startSample = m_currentPositionSamples.load();

            // Mix all active tracks
            mixActiveTracksForBlock(*bufferToFill.buffer, startSample, bufferToFill.numSamples);

            // Update position
            m_currentPositionSamples = startSample + bufferToFill.numSamples;

            // Check if we've reached the end
            Duration_t currentPosMs = getPosition();
            if (currentPosMs >= m_totalDurationMs)
            {
                // Stop at the end - in a real implementation, you might want to signal this
                m_currentPositionSamples = static_cast<juce::int64>((m_totalDurationMs.count() / 1000.0) * m_sampleRate);
            }
        }

        bool MixPlaybackEngine::prepareTrackSources()
        {
            m_trackSources.clear();

            const auto &mixTracks = m_mixLoader->getMixTracks();

            for (const auto &mixTrack : mixTracks)
            {
                const auto *trackInfo = m_mixLoader->getTrackInfoForId(mixTrack.trackId);
                if (!trackInfo)
                {
                    spdlog::error("MixPlaybackEngine: Track info not found for track {}", mixTrack.trackId);
                    continue;
                }

                auto source = std::make_unique<PlaybackTrackSource>(mixTrack.trackId, trackInfo, &mixTrack);

                if (source->prepare(m_formatManager, m_sampleRate, m_blockSize))
                {
                    m_trackSources.push_back(std::move(source));
                }
                else
                {
                    spdlog::error("MixPlaybackEngine: Failed to prepare track {}", mixTrack.trackId);
                }
            }

            spdlog::info("MixPlaybackEngine: Prepared {} track sources", m_trackSources.size());
            
            // If we have a current position, seek all sources to that position
            if (m_currentPositionSamples > 0)
            {
                Duration_t currentPosMs{static_cast<int64_t>((m_currentPositionSamples.load() / m_sampleRate) * 1000.0)};
                spdlog::info("MixPlaybackEngine: Repositioning sources to {} ms after prepare", currentPosMs.count());
                
                // Reuse the positioning logic from setPosition
                for (auto &source : m_trackSources)
                {
                    // Calculate this track's position relative to its start
                    Duration_t trackStartMs = source->mixTrack->mixStartTime;
                    juce::int64 trackStartSamples = static_cast<juce::int64>((trackStartMs.count() / 1000.0) * m_sampleRate);
                    
                    if (m_currentPositionSamples >= trackStartSamples)
                    {
                        // Position is after track start
                        juce::int64 positionInTrack = m_currentPositionSamples - trackStartSamples;
                        
                        // Convert to source sample rate
                        juce::int64 positionInSourceSamples = 0;
                        if (source->reader)
                        {
                            positionInSourceSamples = static_cast<juce::int64>(positionInTrack * source->reader->sampleRate / m_sampleRate);
                        }
                        
                        source->currentPositionInSourceSamples = positionInSourceSamples;
                        
                        if (source->readerSource)
                        {
                            source->readerSource->setNextReadPosition(positionInSourceSamples);
                        }
                    }
                }
            }
            
            return !m_trackSources.empty();
        }

        void MixPlaybackEngine::mixActiveTracksForBlock(juce::AudioBuffer<float> &buffer, juce::int64 startSample, int numSamples)
        {
            const int numChannels = buffer.getNumChannels();

            // Process each track
            for (auto &source : m_trackSources)
            {
                const MixTrack &mixTrack = *source->mixTrack;

                // Calculate track timing in samples
                juce::int64 trackStartSamples = static_cast<juce::int64>((mixTrack.mixStartTime.count() / 1000.0) * m_sampleRate);
                juce::int64 trackDurationSamples = static_cast<juce::int64>((source->trackInfo->duration.count() / 1000.0) * m_sampleRate);
                juce::int64 trackEndSamples = trackStartSamples + trackDurationSamples;

                // Check if this track is active in the current block
                juce::int64 blockEndSample = startSample + numSamples;

                if (trackEndSamples <= startSample || trackStartSamples >= blockEndSample)
                {
                    // Track is not active in this block
                    continue;
                }

                // Calculate the range of samples to read from this track
                juce::int64 trackReadStart = std::max(startSample - trackStartSamples, juce::int64(0));
                juce::int64 trackReadEnd = std::min(blockEndSample - trackStartSamples, trackDurationSamples);
                int samplesToRead = static_cast<int>(trackReadEnd - trackReadStart);

                if (samplesToRead <= 0)
                    continue;

                // Calculate where in the output buffer to place these samples
                int outputOffset = static_cast<int>(std::max(trackStartSamples - startSample, juce::int64(0)));

                // Create a temporary buffer for this track's contribution
                juce::AudioBuffer<float> trackBuffer(numChannels, samplesToRead);
                trackBuffer.clear();

                // Get audio from the source
                juce::AudioSourceChannelInfo trackInfo(&trackBuffer, 0, samplesToRead);

                if (auto *audioSource = source->getAudioSource())
                {
                    audioSource->getNextAudioBlock(trackInfo);
                }

                // Apply envelope and mix into output buffer
                for (int sample = 0; sample < samplesToRead; ++sample)
                {
                    // Calculate time within the track
                    juce::int64 sampleInTrack = trackReadStart + sample;
                    Duration_t timeInTrack{static_cast<int64_t>((sampleInTrack * 1000.0) / m_sampleRate)};

                    // Get envelope gain
                    float gain = getEnvelopeGainForTrack(mixTrack, timeInTrack);

                    // Mix into output buffer
                    int outputSample = outputOffset + sample;
                    if (outputSample >= 0 && outputSample < numSamples)
                    {
                        for (int channel = 0; channel < numChannels; ++channel)
                        {
                            float trackSample = trackBuffer.getSample(channel, sample);
                            float outputSample = buffer.getSample(channel, outputOffset + sample);
                            buffer.setSample(channel, outputOffset + sample, outputSample + (trackSample * gain));
                        }
                    }
                }
            }
        }

        float MixPlaybackEngine::getEnvelopeGainForTrack(const MixTrack &mixTrack, Duration_t timeInTrack)
        {
            return interpolateVolumeFromEnvelope(mixTrack.envelopePoints, timeInTrack);
        }

        //==============================================================================
        // AudioIODeviceCallback implementation
        //==============================================================================
        void MixPlaybackEngine::audioDeviceIOCallbackWithContext(const float *const *inputChannelData,
                                                                 int numInputChannels,
                                                                 float *const *outputChannelData,
                                                                 int numOutputChannels,
                                                                 int numSamples,
                                                                 const juce::AudioIODeviceCallbackContext &context)
        {
            // We ignore input and only produce output
            juce::ignoreUnused(inputChannelData, numInputChannels, context);

            // Create an AudioBuffer wrapper around the output channels
            juce::AudioBuffer<float> buffer(outputChannelData, numOutputChannels, numSamples);
            
            // Clear the buffer first
            buffer.clear();
            
            // Use our existing getNextAudioBlock implementation
            juce::AudioSourceChannelInfo info(&buffer, 0, numSamples);
            getNextAudioBlock(info);
        }

        void MixPlaybackEngine::audioDeviceAboutToStart(juce::AudioIODevice *device)
        {
            if (device)
            {
                prepareToPlay(device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());
            }
        }

        void MixPlaybackEngine::audioDeviceStopped()
        {
            releaseResources();
        }

    } // namespace audio
} // namespace jucyaudio

#endif // MIX_TRANSITION_OLD_PLAYBACK_AVAILABLE