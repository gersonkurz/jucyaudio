#include <Audio/MixPlaybackEngine.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>
#include <Database/Includes/Constants.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace audio
    {
        // Helper function from ExportMixImplementation
        extern float interpolateVolumeFromEnvelope(const std::vector<EnvelopePoint> &envelopePoints, Duration_t timeInTrack);

        //==============================================================================
        // Helper function to calculate track start times using Mix Flow algorithm
        //==============================================================================
        void MixPlaybackEngine::calculateTrackStartTimes()
        {
            spdlog::info("[PlaybackEngine] calculateTrackStartTimes called");
            
            const auto &mixTracks = m_mixLoader->getMixTracks();
            m_trackStartTimes.clear();
            m_trackStartTimes.reserve(mixTracks.size());

            if (mixTracks.empty())
            {
                spdlog::warn("[PlaybackEngine] No tracks to calculate start times for");
                return;
            }

            // Calculate the global offset from the first track's cueStart
            Duration_t globalOffset{0};
            if (mixTracks[0].cueStart < Duration_t{0})
            {
                globalOffset = -mixTracks[0].cueStart;
                spdlog::info("[PlaybackEngine] Global offset: {} ms (from first track cueStart: {} ms)", 
                            globalOffset.count(), mixTracks[0].cueStart.count());
            }

            // Calculate audio start time for each track according to Mix Flow algorithm
            Duration_t previousAudioStartTime{0};
            
            for (size_t i = 0; i < mixTracks.size(); ++i)
            {
                const auto& mixTrack = mixTracks[i];
                Duration_t audioStartTime;

                if (i == 0)
                {
                    // First track starts at the global offset
                    audioStartTime = globalOffset;
                    spdlog::info("[PlaybackEngine] Track 0 starts at {} ms (global offset)", audioStartTime.count());
                }
                else
                {
                    // Subsequent tracks use the placement rule
                    const auto& prevTrack = mixTracks[i - 1];
                    audioStartTime = previousAudioStartTime + prevTrack.attachTo - mixTrack.attachFrom;
                    spdlog::info("[PlaybackEngine] Track {} starts at {} ms (prev {} + attachTo {} - attachFrom {})", 
                                i, audioStartTime.count(), previousAudioStartTime.count(), 
                                prevTrack.attachTo.count(), mixTrack.attachFrom.count());
                }

                m_trackStartTimes.push_back(audioStartTime);
                previousAudioStartTime = audioStartTime;
            }
            
            spdlog::info("[PlaybackEngine] Calculated start times for {} tracks", m_trackStartTimes.size());
        }

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
            const auto trackPath{trackInfo->reconstructFullPath()};
            juce::File sourceFile{ui::jucePathFromFs(trackPath)};
            spdlog::info("[PlaybackTrackSource] Preparing track {} from file: {}", trackId, sourceFile.getFullPathName().toStdString());
            
            if (!sourceFile.exists())
            {
                spdlog::error("[PlaybackTrackSource] File does not exist: {}", sourceFile.getFullPathName().toStdString());
                return false;
            }
            
            reader.reset(formatManager.createReaderFor(sourceFile));

            if (!reader)
            {
                spdlog::error("[PlaybackTrackSource] Failed to create reader for track {} ({})", trackId, pathToString(trackPath));
                return false;
            }

            spdlog::info("[PlaybackTrackSource] Reader created - sample rate: {}, channels: {}, length: {} samples", 
                        reader->sampleRate, reader->numChannels, reader->lengthInSamples);

            readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader.get(), false);

            // Setup resampling if needed
            if (std::abs(reader->sampleRate - targetSampleRate) > 0.01)
            {
                spdlog::info("[PlaybackTrackSource] Setting up resampler from {} Hz to {} Hz", reader->sampleRate, targetSampleRate);
                resampler = std::make_unique<juce::ResamplingAudioSource>(readerSource.get(), false, reader->numChannels);
                resampler->setResamplingRatio(reader->sampleRate / targetSampleRate);
                resampler->prepareToPlay(blockSize, targetSampleRate);
            }
            else
            {
                spdlog::info("[PlaybackTrackSource] No resampling needed, preparing direct playback");
                readerSource->prepareToPlay(blockSize, targetSampleRate);
            }

            return true;
        }

        //==============================================================================
        // MixPlaybackEngine implementation
        //==============================================================================
        MixPlaybackEngine::MixPlaybackEngine()
        {
            spdlog::info("[PlaybackEngine] MixPlaybackEngine constructor");
            m_formatManager.registerBasicFormats();
            m_isPaused = true; // Start paused
            spdlog::info("[PlaybackEngine] Created with paused state");
        }

        MixPlaybackEngine::~MixPlaybackEngine()
        {
            unloadMix();
        }

        bool MixPlaybackEngine::loadMix(MixProjectLoader *mixLoader)
        {
            spdlog::info("[PlaybackEngine] loadMix called with loader: {}", mixLoader ? "valid" : "null");
            
            if (!mixLoader)
            {
                spdlog::error("[PlaybackEngine] Null mix loader provided");
                return false;
            }

            std::lock_guard<std::mutex> lock(m_mutex);

            // Clean up any existing mix (using internal version to avoid recursive lock)
            spdlog::info("[PlaybackEngine] Unloading previous mix");
            unloadMixInternal();

            m_mixLoader = mixLoader;
            spdlog::info("[PlaybackEngine] Mix loader set, mix has {} tracks", m_mixLoader->getMixTracks().size());

            // Calculate track start times using the Mix Flow algorithm
            calculateTrackStartTimes();

            // Calculate total duration
            const auto &mixTracks = m_mixLoader->getMixTracks();
            if (mixTracks.empty())
            {
                spdlog::error("[PlaybackEngine] No tracks in mix");
                return false;
            }

            // Find the last track end time
            Duration_t maxEndTime{0};
            for (size_t i = 0; i < mixTracks.size(); ++i)
            {
                const auto &mixTrack = mixTracks[i];
                if (const auto *trackInfo = m_mixLoader->getTrackInfoForId(mixTrack.trackId))
                {
                    Duration_t trackStartTime = m_trackStartTimes[i];
                    Duration_t trackEndTime = trackStartTime + trackInfo->duration;
                    spdlog::info("[PlaybackEngine] Track {} (id {}) - start: {} ms, duration: {} ms, end: {} ms",
                                i, mixTrack.trackId, trackStartTime.count(), trackInfo->duration.count(), trackEndTime.count());
                    maxEndTime = std::max(maxEndTime, trackEndTime);
                }
                else
                {
                    spdlog::error("[PlaybackEngine] No track info found for track id {}", mixTrack.trackId);
                }
            }

            m_totalDurationMs = maxEndTime;
            spdlog::info("[PlaybackEngine] Total mix duration: {} ms ({})", m_totalDurationMs.count(), durationToString(m_totalDurationMs));

            // Prepare track sources if we're already prepared
            if (m_isPrepared)
            {
                spdlog::info("[PlaybackEngine] Engine already prepared, preparing track sources");
                return prepareTrackSources();
            }
            else
            {
                spdlog::info("[PlaybackEngine] Engine not yet prepared, will prepare sources later");
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
            m_trackStartTimes.clear();

            m_mixLoader = nullptr;
            m_currentPositionSamples = 0;
            m_totalDurationMs = Duration_t{0};
        }
        
        void MixPlaybackEngine::recalculateTrackPositions()
        {
            spdlog::info("[PlaybackEngine] recalculateTrackPositions called");
            
            if (!m_mixLoader)
            {
                spdlog::warn("[PlaybackEngine] recalculateTrackPositions called but no mix loaded");
                return;
            }
            
            std::lock_guard<std::mutex> lock(m_mutex);
            
            // Store the current playback position so we can maintain it
            const auto currentPos = getPosition();
            spdlog::info("[PlaybackEngine] Current position before recalc: {} ms", currentPos.count());
            
            // Recalculate track start times using the updated Mix Flow algorithm
            calculateTrackStartTimes();
            
            // Recalculate total duration
            const auto &mixTracks = m_mixLoader->getMixTracks();
            Duration_t maxEndTime{0};
            for (size_t i = 0; i < mixTracks.size(); ++i)
            {
                const auto &mixTrack = mixTracks[i];
                if (const auto *trackInfo = m_mixLoader->getTrackInfoForId(mixTrack.trackId))
                {
                    Duration_t trackStartTime = m_trackStartTimes[i];
                    Duration_t trackEndTime = trackStartTime + trackInfo->duration;
                    spdlog::info("[PlaybackEngine] Track {} (id {}) - recalculated start: {} ms, end: {} ms",
                                i, mixTrack.trackId, trackStartTime.count(), trackEndTime.count());
                    maxEndTime = std::max(maxEndTime, trackEndTime);
                }
            }
            
            m_totalDurationMs = maxEndTime;
            spdlog::info("[PlaybackEngine] Recalculated total mix duration: {} ms", m_totalDurationMs.count());
            
            // Maintain the playback position
            setPositionInternal(currentPos);
            spdlog::info("[PlaybackEngine] Track positions recalculated successfully");
        }

        void MixPlaybackEngine::setPositionInternal(Duration_t positionMs)
        {
            // Internal version - assumes mutex is already locked
            spdlog::info("[PlaybackEngine] setPositionInternal called with {} ms", positionMs.count());
            
            if (!m_mixLoader)
            {
                spdlog::warn("[PlaybackEngine] setPositionInternal called but no mix loaded");
                return;
            }
            
            // Clamp position to valid range
            Duration_t clampedPosition = std::max(Duration_t{0}, std::min(positionMs, m_totalDurationMs));
            if (clampedPosition != positionMs)
            {
                spdlog::info("[PlaybackEngine] Position clamped from {} ms to {} ms (total duration: {} ms)",
                            positionMs.count(), clampedPosition.count(), m_totalDurationMs.count());
            }
            positionMs = clampedPosition;

            // Convert to samples
            juce::int64 positionSamples = static_cast<juce::int64>((positionMs.count() / 1000.0) * m_sampleRate);
            spdlog::info("[PlaybackEngine] Position {} ms = {} samples (sample rate: {})", 
                        positionMs.count(), positionSamples, m_sampleRate);

            m_currentPositionSamples = positionSamples;

            // Update all track source positions
            for (size_t i = 0; i < m_trackSources.size(); ++i)
            {
                auto &source = m_trackSources[i];
                
                // Get the track's start time from our calculated positions
                if (i >= m_trackStartTimes.size())
                {
                    spdlog::error("MixPlaybackEngine: Track index {} out of bounds for start times", i);
                    continue;
                }
                
                Duration_t trackStartMs = m_trackStartTimes[i];
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
        
        void MixPlaybackEngine::setPosition(Duration_t positionMs)
        {
            spdlog::info("[PlaybackEngine] setPosition called with {} ms", positionMs.count());
            
            std::lock_guard<std::mutex> lock(m_mutex);
            setPositionInternal(positionMs);
        }

        Duration_t MixPlaybackEngine::getPosition() const
        {
            double positionSeconds = m_currentPositionSamples.load() / m_sampleRate;
            return Duration_t{static_cast<int64_t>(positionSeconds * 1000.0)};
        }

        void MixPlaybackEngine::setPaused(bool shouldPause)
        {
            spdlog::info("[PlaybackEngine] setPaused called with {}", shouldPause);
            m_isPaused = shouldPause;
        }

        void MixPlaybackEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
        {
            spdlog::info("[PlaybackEngine] prepareToPlay called - sample rate: {}, block size: {}", 
                        sampleRate, samplesPerBlockExpected);
            
            std::lock_guard<std::mutex> lock(m_mutex);

            // Store current position before preparing
            auto currentPos = m_currentPositionSamples.load();
            spdlog::info("[PlaybackEngine] Current position before prepare: {} samples", currentPos);
            
            m_sampleRate = sampleRate;
            m_blockSize = samplesPerBlockExpected;
            m_isPrepared = true;

            if (m_mixLoader)
            {
                spdlog::info("[PlaybackEngine] Mix loader present, preparing sources");
                
                // Recalculate track start times in case they weren't calculated yet
                if (m_trackStartTimes.empty())
                {
                    spdlog::info("[PlaybackEngine] Track start times empty, recalculating");
                    calculateTrackStartTimes();
                }
                
                prepareTrackSources();
                
                // Restore position after preparing track sources
                if (currentPos > 0)
                {
                    m_currentPositionSamples = currentPos;
                    spdlog::info("[PlaybackEngine] Restored position to {} samples after prepare", currentPos);
                }
            }
            else
            {
                spdlog::warn("[PlaybackEngine] No mix loader present during prepareToPlay");
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
            // Log only occasionally to avoid spam
            static int blockCount = 0;
            if (++blockCount % 100 == 1)  // Log every 100th block
            {
                spdlog::debug("[PlaybackEngine] getNextAudioBlock - loader: {}, prepared: {}, paused: {}",
                             m_mixLoader ? "yes" : "no", m_isPrepared.load(), m_isPaused.load());
            }
            
            if (!m_mixLoader || !m_isPrepared || m_isPaused)
            {
                bufferToFill.clearActiveBufferRegion();
                if (blockCount % 100 == 1)
                {
                    spdlog::debug("[PlaybackEngine] Not playing - clearing buffer");
                }
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
                spdlog::info("[PlaybackEngine] Reached end of mix at {} ms", currentPosMs.count());
                // Stop at the end - in a real implementation, you might want to signal this
                m_currentPositionSamples = static_cast<juce::int64>((m_totalDurationMs.count() / 1000.0) * m_sampleRate);
            }
            
            if (blockCount % 100 == 1)
            {
                spdlog::debug("[PlaybackEngine] Audio block processed - position: {} ms", currentPosMs.count());
            }
        }

        bool MixPlaybackEngine::prepareTrackSources()
        {
            
            spdlog::info("[PlaybackEngine] prepareTrackSources called");
            m_trackSources.clear();

            const auto &mixTracks = m_mixLoader->getMixTracks();
            spdlog::info("[PlaybackEngine] Preparing {} tracks", mixTracks.size());

            for (size_t i = 0; i < mixTracks.size(); ++i)
            {
                const auto &mixTrack = mixTracks[i];
                const auto *trackInfo = m_mixLoader->getTrackInfoForId(mixTrack.trackId);
                if (!trackInfo)
                {
                    spdlog::error("[PlaybackEngine] Track info not found for track {} (index {})", mixTrack.trackId, i);
                    continue;
                }
                const auto trackPath{trackInfo->reconstructFullPath()};
                spdlog::info("[PlaybackEngine] Preparing track {} - id: {}, file: {}", 
                            i, mixTrack.trackId, pathToString(trackPath));

                auto source = std::make_unique<PlaybackTrackSource>(mixTrack.trackId, trackInfo, &mixTrack);

                if (source->prepare(m_formatManager, m_sampleRate, m_blockSize))
                {
                    m_trackSources.push_back(std::move(source));
                    spdlog::info("[PlaybackEngine] Track {} prepared successfully", i);
                }
                else
                {
                    spdlog::error("[PlaybackEngine] Failed to prepare track {} ({})", mixTrack.trackId, pathToString(trackPath));
                }
            }

            spdlog::info("[PlaybackEngine] Successfully prepared {} out of {} track sources", m_trackSources.size(), mixTracks.size());
            
            // If we have a current position, seek all sources to that position
            if (m_currentPositionSamples > 0)
            {
                Duration_t currentPosMs{static_cast<int64_t>((m_currentPositionSamples.load() / m_sampleRate) * 1000.0)};
                spdlog::info("[PlaybackEngine] Repositioning sources to {} ms after prepare", currentPosMs.count());
                
                // Reuse the positioning logic from setPosition
                for (size_t i = 0; i < m_trackSources.size(); ++i)
                {
                    auto &source = m_trackSources[i];
                    
                    // Get the track's start time from our calculated positions
                    if (i >= m_trackStartTimes.size())
                    {
                        spdlog::error("MixPlaybackEngine: Track index {} out of bounds for start times", i);
                        continue;
                    }
                    
                    Duration_t trackStartMs = m_trackStartTimes[i];
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
            for (size_t i = 0; i < m_trackSources.size(); ++i)
            {
                auto &source = m_trackSources[i];
                const MixTrack &mixTrack = *source->mixTrack;

                // Get the track's start time from our calculated positions
                if (i >= m_trackStartTimes.size())
                {
                    spdlog::error("MixPlaybackEngine: Track index {} out of bounds for start times", i);
                    continue;
                }

                // Calculate track timing in samples
                juce::int64 trackStartSamples = static_cast<juce::int64>((m_trackStartTimes[i].count() / 1000.0) * m_sampleRate);
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

