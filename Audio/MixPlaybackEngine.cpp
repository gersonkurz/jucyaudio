#include <Audio/MixPlaybackEngine.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>
#include <Database/Includes/Constants.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace audio
    {
        extern float interpolateVolumeFromEnvelope(const std::vector<EnvelopePoint> &envelopePoints, Duration_t timeInTrack);

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

            Duration_t previousAudioStartTime{0};
            
            for (size_t i = 0; i < mixTracks.size(); ++i)
            {
                const auto& mixTrack = mixTracks[i];
                Duration_t audioStartTime;

                if (i == 0)
                {
                    audioStartTime = Duration_t{0};
                }
                else
                {
                    const auto& prevTrack = mixTracks[i - 1];
                    audioStartTime = previousAudioStartTime + prevTrack.attachTo - mixTrack.attachFrom;
                }

                m_trackStartTimes.push_back(audioStartTime);
                previousAudioStartTime = audioStartTime;
            }
        }

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
            
            // Check if filename has special characters
            const bool hasSpecialChars = trackInfo->filename.find('{') != std::string::npos ||
                                        trackInfo->filename.find('}') != std::string::npos;
            
            if (hasSpecialChars) {
                spdlog::info("[AUDIO DEBUG] Track {} has special chars in filename: {}", 
                            trackId, trackInfo->filename);
                spdlog::info("[AUDIO DEBUG] Full reconstructed path: {}", trackPath.string());
                spdlog::info("[AUDIO DEBUG] JUCE file path: {}", sourceFile.getFullPathName().toStdString());
            }
            
            spdlog::info("[AUDIO DEBUG] Preparing track {} from file: {}", 
                        trackId, trackPath.string());
            
            if (!sourceFile.exists())
            {
                spdlog::error("[AUDIO DEBUG] File does not exist: {}", trackPath.string());
                return false;
            }
            
            reader.reset(formatManager.createReaderFor(sourceFile));

            if (!reader)
            {
                spdlog::error("[AUDIO DEBUG] Failed to create reader for: {}", trackPath.string());
                return false;
            }

            // Log detailed audio format information
            spdlog::info("[AUDIO DEBUG] Track {} audio format: channels={}, sampleRate={}, bitsPerSample={}, formatName={}",
                        trackId,
                        reader->numChannels,
                        reader->sampleRate,
                        reader->bitsPerSample,
                        reader->getFormatName().toStdString());

            readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader.get(), false);

            if (std::abs(reader->sampleRate - targetSampleRate) > 0.01)
            {
                spdlog::info("[AUDIO DEBUG] Track {} needs resampling from {} to {} Hz",
                            trackId, reader->sampleRate, targetSampleRate);
                resampler = std::make_unique<juce::ResamplingAudioSource>(readerSource.get(), false, reader->numChannels);
                resampler->setResamplingRatio(reader->sampleRate / targetSampleRate);
                resampler->prepareToPlay(blockSize, targetSampleRate);
            }
            else
            {
                spdlog::info("[AUDIO DEBUG] Track {} no resampling needed (rate={})",
                            trackId, reader->sampleRate);
                readerSource->prepareToPlay(blockSize, targetSampleRate);
            }

            return true;
        }

        MixPlaybackEngine::MixPlaybackEngine()
        {
            m_formatManager.registerBasicFormats();
            m_isPaused = true;
        }

        MixPlaybackEngine::~MixPlaybackEngine()
        {
            unloadMix();
        }

        bool MixPlaybackEngine::loadMix(MixProjectLoader *mixLoader)
        {
            spdlog::debug("JUCYAUDIO: MixPlaybackEngine::loadMix -> Entry");
            if (!mixLoader)
            {
                return false;
            }

            const juce::ScopedLock lock(m_critSec);

            unloadMixInternal();

            m_mixLoader = mixLoader;

            calculateTrackStartTimes();

            const auto &mixTracks = m_mixLoader->getMixTracks();
            if (mixTracks.empty())
            {
                return false;
            }

            Duration_t maxEndTime{0};
            for (size_t i = 0; i < mixTracks.size(); ++i)
            {
                const auto &mixTrack = mixTracks[i];
                if (const auto *trackInfo = m_mixLoader->getTrackInfoForId(mixTrack.trackId))
                {
                    Duration_t trackStartTime = m_trackStartTimes[i];
                    Duration_t trackEndTime = trackStartTime + trackInfo->duration;
                    maxEndTime = std::max(maxEndTime, trackEndTime);
                }
            }

            m_totalDurationMs = maxEndTime;

            if (m_isPrepared)
            {
                spdlog::debug("JUCYAUDIO: MixPlaybackEngine::loadMix -> Engine already prepared, preparing sources now.");
                return prepareTrackSources();
            }

            spdlog::debug("JUCYAUDIO: MixPlaybackEngine::loadMix -> Exit (success, sources will be prepared later)");
            return true;
        }

        void MixPlaybackEngine::unloadMix()
        {
            const juce::ScopedLock lock(m_critSec);
            unloadMixInternal();
        }

        void MixPlaybackEngine::unloadMixInternal()
        {
            spdlog::debug("JUCYAUDIO: MixPlaybackEngine::unloadMixInternal -> Entry");
            m_isPaused = true;
            
            for (auto &source : m_trackSources)
            {
                if (source && source->getAudioSource())
                {
                    source->getAudioSource()->releaseResources();
                }
            }
            m_trackSources.clear();
            m_trackStartTimes.clear();

            m_mixLoader = nullptr;
            m_currentPositionSamples = 0;
            m_totalDurationMs = Duration_t{0};
            spdlog::debug("JUCYAUDIO: MixPlaybackEngine::unloadMixInternal -> Exit");
        }
        
        void MixPlaybackEngine::recalculateTrackPositions()
        {
            if (!m_mixLoader)
            {
                return;
            }
            
            const juce::ScopedLock lock(m_critSec);
            
            const auto currentPos = getPosition();
            
            calculateTrackStartTimes();
            
            const auto &mixTracks = m_mixLoader->getMixTracks();
            Duration_t maxEndTime{0};
            for (size_t i = 0; i < mixTracks.size(); ++i)
            {
                const auto &mixTrack = mixTracks[i];
                if (const auto *trackInfo = m_mixLoader->getTrackInfoForId(mixTrack.trackId))
                {
                    Duration_t trackStartTime = m_trackStartTimes[i];
                    Duration_t trackEndTime = trackStartTime + trackInfo->duration;
                    maxEndTime = std::max(maxEndTime, trackEndTime);
                }
            }
            
            m_totalDurationMs = maxEndTime;
            
            setPositionInternal(currentPos);
        }

        void MixPlaybackEngine::setPositionInternal(Duration_t positionMs)
        {
            spdlog::debug("JUCYAUDIO: setPositionInternal -> Entry. Requested ms: {}, Total duration ms: {}", positionMs.count(), m_totalDurationMs.count());
            if (!m_mixLoader)
            {
                spdlog::debug("JUCYAUDIO: setPositionInternal -> Exit (no mix loader)");
                return;
            }
            
            Duration_t clampedPosition = std::max(Duration_t{0}, std::min(positionMs, m_totalDurationMs));
            
            juce::int64 positionSamples = static_cast<juce::int64>((clampedPosition.count() / 1000.0) * m_sampleRate);
            spdlog::debug("JUCYAUDIO: setPositionInternal -> Calculated samples: {} (from {}ms)", positionSamples, clampedPosition.count());

            m_currentPositionSamples = positionSamples;
            spdlog::debug("JUCYAUDIO: setPositionInternal -> m_currentPositionSamples is now {}", m_currentPositionSamples.load());

            for (size_t i = 0; i < m_trackSources.size(); ++i)
            {
                auto &source = m_trackSources[i];
                
                if (i >= m_trackStartTimes.size())
                {
                    continue;
                }
                
                Duration_t trackStartMs = m_trackStartTimes[i];
                juce::int64 trackStartSamples = static_cast<juce::int64>((trackStartMs.count() / 1000.0) * m_sampleRate);

                if (positionSamples >= trackStartSamples)
                {
                    juce::int64 positionInTrack = positionSamples - trackStartSamples;

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
                    source->currentPositionInSourceSamples = 0;
                    if (source->readerSource)
                    {
                        source->readerSource->setNextReadPosition(0);
                    }
                }
            }
            spdlog::debug("JUCYAUDIO: setPositionInternal -> Exit");
        }
        
        void MixPlaybackEngine::setPosition(Duration_t positionMs)
        {
            spdlog::debug("JUCYAUDIO: MixPlaybackEngine::setPosition -> Entry, ms: {}", positionMs.count());
            const juce::ScopedLock lock(m_critSec);
            setPositionInternal(positionMs);
            spdlog::debug("JUCYAUDIO: MixPlaybackEngine::setPosition -> Exit");
        }

        Duration_t MixPlaybackEngine::getPosition() const
        {
            double positionSeconds = m_currentPositionSamples.load() / m_sampleRate;
            return Duration_t{static_cast<int64_t>(positionSeconds * 1000.0)};
        }

        void MixPlaybackEngine::setPaused(bool shouldPause)
        {
            spdlog::debug("JUCYAUDIO: MixPlaybackEngine::setPaused -> Setting paused to: {}", shouldPause);
            m_isPaused = shouldPause;
        }

        void MixPlaybackEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
        {
            const juce::ScopedLock lock(m_critSec);

            auto currentPos = m_currentPositionSamples.load();
            
            m_sampleRate = sampleRate;
            m_blockSize = samplesPerBlockExpected;
            m_isPrepared = true;

            if (m_mixLoader)
            {
                if (m_trackStartTimes.empty())
                {
                    calculateTrackStartTimes();
                }
                
                prepareTrackSources();
                
                if (currentPos > 0)
                {
                    m_currentPositionSamples = currentPos;
                }
            }
        }

        void MixPlaybackEngine::releaseResources()
        {
            const juce::ScopedLock lock(m_critSec);

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

            const juce::ScopedLock lock(m_critSec);
            
            if (!m_mixLoader || m_trackSources.empty())
            {
                bufferToFill.clearActiveBufferRegion();
                return;
            }

            // Log buffer info periodically
            static int audioBlockCount = 0;
            if (++audioBlockCount % 100 == 1) {
                spdlog::info("[AUDIO DEBUG] getNextAudioBlock: buffer channels={}, startChannel={}, numSamples={}",
                            bufferToFill.buffer->getNumChannels(),
                            bufferToFill.startSample, 
                            bufferToFill.numSamples);
            }

            bufferToFill.clearActiveBufferRegion();

            juce::int64 startSample = m_currentPositionSamples.load();

            mixActiveTracksForBlock(*bufferToFill.buffer, startSample, bufferToFill.numSamples);

            m_currentPositionSamples = startSample + bufferToFill.numSamples;

            Duration_t currentPosMs = getPosition();
            if (currentPosMs >= m_totalDurationMs)
            {
                m_currentPositionSamples = static_cast<juce::int64>((m_totalDurationMs.count() / 1000.0) * m_sampleRate);
            }
        }

        bool MixPlaybackEngine::prepareTrackSources()
        {
            m_trackSources.clear();

            const auto &mixTracks = m_mixLoader->getMixTracks();

            for (size_t i = 0; i < mixTracks.size(); ++i)
            {
                const auto &mixTrack = mixTracks[i];
                const auto *trackInfo = m_mixLoader->getTrackInfoForId(mixTrack.trackId);
                if (!trackInfo)
                {
                    continue;
                }
                const auto trackPath{trackInfo->reconstructFullPath()};

                auto source = std::make_unique<PlaybackTrackSource>(mixTrack.trackId, trackInfo, &mixTrack);

                if (source->prepare(m_formatManager, m_sampleRate, m_blockSize))
                {
                    m_trackSources.push_back(std::move(source));
                }
            }

            if (m_currentPositionSamples > 0)
            {
                Duration_t currentPosMs{static_cast<int64_t>((m_currentPositionSamples.load() / m_sampleRate) * 1000.0)};
                
                for (size_t i = 0; i < m_trackSources.size(); ++i)
                {
                    auto &source = m_trackSources[i];
                    
                    if (i >= m_trackStartTimes.size())
                    {
                        continue;
                    }
                    
                    Duration_t trackStartMs = m_trackStartTimes[i];
                    juce::int64 trackStartSamples = static_cast<juce::int64>((trackStartMs.count() / 1000.0) * m_sampleRate);
                    
                    if (m_currentPositionSamples >= trackStartSamples)
                    {
                        juce::int64 positionInTrack = m_currentPositionSamples - trackStartSamples;
                        
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
            int activeTracksThisBlock = 0;
            
            static int mixBlockCount = 0;
            const bool shouldLogDetails = (++mixBlockCount % 100 == 1);
            
            if (shouldLogDetails) {
                spdlog::info("[AUDIO DEBUG] mixActiveTracksForBlock: output buffer channels={}, numSamples={}", 
                            numChannels, numSamples);
            }

            for (size_t i = 0; i < m_trackSources.size(); ++i)
            {
                auto &source = m_trackSources[i];
                
                if (!source || !source->mixTrack || !source->trackInfo)
                {
                    continue;
                }
                
                const MixTrack &mixTrack = *source->mixTrack;

                if (i >= m_trackStartTimes.size())
                {
                    continue;
                }

                const auto trackStartSamples = static_cast<juce::int64>((m_trackStartTimes[i].count() / 1000.0) * m_sampleRate);
                const auto trackDurationSamples = static_cast<juce::int64>((source->trackInfo->duration.count() / 1000.0) * m_sampleRate);
                const auto trackEndSamples = trackStartSamples + trackDurationSamples;

                const auto blockEndSample = startSample + numSamples;

                if (trackEndSamples <= startSample || trackStartSamples >= blockEndSample)
                {
                    continue;
                }

                activeTracksThisBlock++;
                const auto trackReadStart = std::max(startSample - trackStartSamples, juce::int64(0));
                const auto trackReadEnd = std::min(blockEndSample - trackStartSamples, trackDurationSamples);
                int samplesToRead = static_cast<int>(trackReadEnd - trackReadStart);

                if (samplesToRead <= 0)
                    continue;

                int outputOffset = static_cast<int>(std::max(trackStartSamples - startSample, juce::int64(0)));

                // Check if this track has special characters in filename
                const bool hasSpecialChars = source->trackInfo->filename.find('{') != std::string::npos ||
                                            source->trackInfo->filename.find('}') != std::string::npos;
                
                // Create track buffer - check if we need to handle mono->stereo conversion
                const int sourceChannels = source->reader ? source->reader->numChannels : numChannels;
                juce::AudioBuffer<float> trackBuffer(numChannels, samplesToRead);
                trackBuffer.clear();

                if (shouldLogDetails && hasSpecialChars) {
                    spdlog::info("[AUDIO DEBUG] Processing track {} with special chars, source channels={}, buffer channels={}",
                                source->trackId, sourceChannels, numChannels);
                }

                juce::AudioSourceChannelInfo trackInfo(&trackBuffer, 0, samplesToRead);

                if (auto *audioSource = source->getAudioSource())
                {
                    audioSource->getNextAudioBlock(trackInfo);
                    
                    // Handle mono-to-stereo conversion if needed
                    if (sourceChannels == 1 && numChannels == 2) {
                        // Source is mono but output is stereo - duplicate mono channel to right channel
                        for (int sample = 0; sample < samplesToRead; ++sample) {
                            const float monoSample = trackBuffer.getSample(0, sample);
                            trackBuffer.setSample(1, sample, monoSample);
                        }
                        
                        if (shouldLogDetails && hasSpecialChars) {
                            spdlog::info("[AUDIO DEBUG] Duplicated mono channel to stereo for track {}", source->trackId);
                        }
                    }
                    
                    // Log if we're getting mono data when we expect stereo
                    if (shouldLogDetails && hasSpecialChars && numChannels == 2) {
                        // Check if right channel is silent (indicating mono playback)
                        float leftMax = 0.0f, rightMax = 0.0f;
                        for (int s = 0; s < std::min(100, samplesToRead); ++s) {
                            leftMax = std::max(leftMax, std::abs(trackBuffer.getSample(0, s)));
                            if (numChannels > 1) {
                                rightMax = std::max(rightMax, std::abs(trackBuffer.getSample(1, s)));
                            }
                        }
                        spdlog::info("[AUDIO DEBUG] Track {} channel levels after conversion: L={:.4f}, R={:.4f}",
                                    source->trackId, leftMax, rightMax);
                    }
                }

                const float masterGain = m_masterGain.load();
                for (int sample = 0; sample < samplesToRead; ++sample)
                {
                    const auto sampleInTrack = trackReadStart + sample;
                    Duration_t timeInTrack{static_cast<int64_t>((sampleInTrack * 1000.0) / m_sampleRate)};

                    float gain = getEnvelopeGainForTrack(mixTrack, timeInTrack) * masterGain * mixTrack.gainAdjustment; // Apply per-track gain adjustment

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
            
            if (shouldLogDetails) {
                spdlog::info("[AUDIO DEBUG] mixActiveTracksForBlock: {} active tracks processed", activeTracksThisBlock);
            }
        }

        float MixPlaybackEngine::getEnvelopeGainForTrack(const MixTrack &mixTrack, Duration_t timeInTrack)
        {
            return interpolateVolumeFromEnvelope(mixTrack.envelopePoints, timeInTrack);
        }

        void MixPlaybackEngine::audioDeviceIOCallbackWithContext(const float *const *inputChannelData,
                                                                 int numInputChannels,
                                                                 float *const *outputChannelData,
                                                                 int numOutputChannels,
                                                                 int numSamples,
                                                                 const juce::AudioIODeviceCallbackContext &context)
        {
            juce::ignoreUnused(inputChannelData, numInputChannels, context);

            juce::AudioBuffer<float> buffer(outputChannelData, numOutputChannels, numSamples);
            
            buffer.clear();
            
            juce::AudioSourceChannelInfo info(&buffer, 0, numSamples);

            static int blockCount = 0;
            if (++blockCount % 100 == 1) { // Log every 100 blocks
                spdlog::debug("JUCYAUDIO: audioDeviceIOCallback -> paused={}, prepared={}, pos={}, samples={}", 
                    m_isPaused.load(), m_isPrepared.load(), m_currentPositionSamples.load(), numSamples);
            }

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