#include <Audio/MixPlaybackEngine.h>
#include <Audio/AudioUtils.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>
#include <Database/Includes/Constants.h>
#include <spdlog/spdlog.h>
#include <unordered_map>

namespace jucyaudio
{
    namespace audio
    {
        std::shared_ptr<PlaybackState> MixPlaybackEngine::buildPlaybackState(MixProjectLoader* mixLoader)
        {
            if (!mixLoader)
            {
                spdlog::error("[PlaybackEngine] buildPlaybackState -> mixLoader is null");
                return nullptr;
            }

            // Create new PlaybackState wrapped in shared_ptr
            auto state = std::make_shared<PlaybackState>();
            spdlog::info("[PlaybackEngine] buildPlaybackState -> Created new PlaybackState");

            // Copy TrackInfo data from MixProjectLoader
            const auto& mixTracks = mixLoader->getMixTracks();
            state->trackInfos.reserve(mixTracks.size());

            for (const auto& mixTrack : mixTracks)
            {
                if (const auto* trackInfo = mixLoader->getTrackInfoForId(mixTrack.trackId))
                {
                    state->trackInfos.push_back(*trackInfo);  // Copy TrackInfo by value
                }
            }

            // Build O(1) lookup map for TrackInfo (avoids O(n²) linear searches)
            std::unordered_map<TrackId, const TrackInfo*> trackInfoMap;
            trackInfoMap.reserve(state->trackInfos.size());
            for (const auto& ti : state->trackInfos)
            {
                trackInfoMap[ti.trackId] = &ti;
            }

            // Calculate track start times using Mix Flow algorithm
            state->trackStartTimes.reserve(mixTracks.size());
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

                state->trackStartTimes.push_back(audioStartTime);
                previousAudioStartTime = audioStartTime;
            }

            // Calculate total duration using O(1) map lookups
            // Use effective duration from cue points
            Duration_t maxEndTime{0};
            for (size_t i = 0; i < mixTracks.size(); ++i)
            {
                const auto& mixTrack = mixTracks[i];
                auto it = trackInfoMap.find(mixTrack.trackId);
                if (it != trackInfoMap.end())
                {
                    Duration_t trackStartTime = state->trackStartTimes[i];
                    Duration_t trackEndTime = trackStartTime + mixTrack.getEffectiveDuration(it->second->duration);
                    maxEndTime = std::max(maxEndTime, trackEndTime);
                }
            }
            state->totalDuration = maxEndTime;

            // Build PlaybackTrackSource objects (only if engine is prepared)
            if (m_isPrepared)
            {
                spdlog::debug("[PlaybackEngine] buildPlaybackState -> Engine is prepared, creating track sources");
                state->trackSources.reserve(mixTracks.size());

                // Salvage already-warmed readers from the current state so a rebuild (attach
                // edit, track removal, ...) reuses their MP3 frame tables instead of creating
                // fresh readers that must re-scan on the audio thread. Only fully-warmed
                // readers are eligible: a still-warming reader would race the warm thread.
                // Safe: the audio thread reaches a reader only via readerSource (a raw pointer),
                // never via this shared_ptr, and copying the shared_ptr here just bumps its
                // atomic refcount.
                std::unordered_map<TrackId, std::shared_ptr<juce::AudioFormatReader>> reusableReaders;
                if (auto oldState = m_currentPlaybackState.load())
                {
                    for (const auto &s : oldState->trackSources)
                        if (s && s->reader && s->warmed.load(std::memory_order_acquire))
                            reusableReaders.emplace(s->trackId, s->reader);
                }

                for (size_t i = 0; i < mixTracks.size(); ++i)
                {
                    const auto& mixTrack = mixTracks[i];
                    auto it = trackInfoMap.find(mixTrack.trackId);
                    if (it == trackInfoMap.end())
                    {
                        spdlog::error("[PlaybackEngine] buildPlaybackState -> TrackInfo not found for trackId={}; this track will be silent", mixTrack.trackId);
                        continue;
                    }
                    const auto* trackInfo = it->second;

                    auto source = std::make_unique<PlaybackTrackSource>(mixTrack.trackId, i, trackInfo, mixTrack);

                    std::shared_ptr<juce::AudioFormatReader> reused;
                    if (auto rit = reusableReaders.find(mixTrack.trackId); rit != reusableReaders.end())
                        reused = rit->second;

                    if (source->prepare(m_formatManager, m_sampleRate, m_blockSize, state->trackStartTimes[i], std::move(reused)))
                    {
                        state->trackSources.push_back(std::move(source));
                    }
                    else
                    {
                        spdlog::error("[PlaybackEngine] buildPlaybackState -> Failed to prepare track source for trackId={}", mixTrack.trackId);
                    }
                }
            }

            // Pre-build MP3 seek indexes off the audio thread so scrubbing doesn't stall the
            // realtime callback. Reused readers are already warmed; this only touches new
            // (first-seen) readers, and only while paused - setPaused(false) stops and joins
            // the pool before the audio thread reads anything, so warming never races playback
            // (that's why mixActiveTracksForBlock has no readiness gate).
            //
            // Bounded on two axes so a huge mix (e.g. 1000+ tracks) can't explode: at most
            // kMaxEagerWarm tracks, in timeline order (you play from the start), via a pool of
            // at most hardware_concurrency workers. Un-warmed tracks simply pay JUCE's
            // forward-scan on their first seek - slower, but they always play.
            const bool warmInBackground = m_isPaused.load(std::memory_order_acquire);
            if (warmInBackground)
            {
                constexpr size_t kMaxEagerWarm = 32;
                auto toWarm = std::make_shared<std::vector<PlaybackTrackSource *>>();
                for (auto &s : state->trackSources)
                {
                    if (s && !s->warmed.load(std::memory_order_acquire))
                    {
                        toWarm->push_back(s.get());
                        if (toWarm->size() >= kMaxEagerWarm)
                            break;
                    }
                }

                if (!toWarm->empty())
                {
                    PlaybackState *raw = state.get();
                    auto nextIndex = std::make_shared<std::atomic<size_t>>(0);
                    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
                    const unsigned workers = std::min<unsigned>(hw, static_cast<unsigned>(toWarm->size()));
                    for (unsigned w = 0; w < workers; ++w)
                    {
                        state->warmThreads.emplace_back(
                            [raw, toWarm, nextIndex]
                            {
                                for (;;)
                                {
                                    if (raw->warmStop.load(std::memory_order_acquire))
                                        return;
                                    const size_t i = nextIndex->fetch_add(1, std::memory_order_relaxed);
                                    if (i >= toWarm->size())
                                        return;
                                    (*toWarm)[i]->warmSeekIndex(raw->warmStop);
                                }
                            });
                    }
                }
            }

            spdlog::debug("[PlaybackEngine] buildPlaybackState -> Exit (state created with {} tracks, {} sources)",
                         state->trackInfos.size(), state->trackSources.size());

            return state;
        }

        void MixPlaybackEngine::collectGarbage()
        {
            // Scan garbage list and remove any states that are no longer referenced by the audio thread.
            // If use_count() == 1, it means m_garbage is the only holder, so audio thread is done.
            auto it = m_garbage.begin();
            while (it != m_garbage.end())
            {
                if (it->use_count() == 1)
                {
                    // Safe to delete - destructor runs here on main thread
                    it = m_garbage.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        PlaybackTrackSource::PlaybackTrackSource(TrackId id, size_t index, const TrackInfo *ti, const MixTrack& mt)
            : trackId{id},
              mixTrackIndex{index},
              trackInfo{ti},
              mixTrack{mt}  // Copy MixTrack by value
        {
        }

        bool PlaybackTrackSource::prepare(juce::AudioFormatManager &formatManager, double targetSampleRate, int blockSize,
                                           Duration_t trackStartTime,
                                           std::shared_ptr<juce::AudioFormatReader> reusedReader)
        {
            const auto trackPath{trackInfo->reconstructFullPath()};
            juce::File sourceFile{ui::jucePathFromFs(trackPath)};

            // Check if filename has special characters
            const bool hasSpecialChars = trackInfo->filename.find('{') != std::string::npos ||
                                        trackInfo->filename.find('}') != std::string::npos;

            if (hasSpecialChars) {
                spdlog::debug("Track {} has special chars in filename: {}",
                            trackId, trackInfo->filename);
                spdlog::debug("Full reconstructed path: {}", trackPath.string());
                spdlog::debug("JUCE file path: {}", sourceFile.getFullPathName().toStdString());
            }

            spdlog::debug("Preparing track {} from file: {}",
                        trackId, trackPath.string());

            if (!sourceFile.exists())
            {
                spdlog::error("[AUDIO DEBUG] File does not exist: {}", trackPath.string());
                return false;
            }

            const bool didReuse = (reusedReader != nullptr);
            if (didReuse)
            {
                // Inherited from the previous PlaybackState: its MP3 frame table is already
                // built, so seeks stay fast and no re-warm is needed.
                reader = std::move(reusedReader);
                spdlog::info("[MP3-WARM] Reusing warmed reader for track {}", trackId);
            }
            else
            {
                reader.reset(formatManager.createReaderFor(sourceFile));
            }

            if (!reader)
            {
                spdlog::error("[AUDIO DEBUG] Failed to create reader for: {}", trackPath.string());
                return false;
            }

            // Log detailed audio format information
            spdlog::debug("Track {} audio format: channels={}, sampleRate={}, bitsPerSample={}, formatName={}",
                        trackId,
                        reader->numChannels,
                        reader->sampleRate,
                        reader->bitsPerSample,
                        reader->getFormatName().toStdString());

            readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader.get(), false);

            if (std::abs(reader->sampleRate - targetSampleRate) > 0.01)
            {
                spdlog::debug("Track {} needs resampling from {} to {} Hz",
                            trackId, reader->sampleRate, targetSampleRate);
                resampler = std::make_unique<juce::ResamplingAudioSource>(readerSource.get(), false, reader->numChannels);
                resampler->setResamplingRatio(reader->sampleRate / targetSampleRate);
                resampler->prepareToPlay(blockSize, targetSampleRate);
            }
            else
            {
                spdlog::debug("Track {} no resampling needed (rate={})",
                            trackId, reader->sampleRate);
                readerSource->prepareToPlay(blockSize, targetSampleRate);
            }

            // Pre-calculate sample positions at target sample rate (avoids per-block math)
            // Use effective duration from cue points
            startSampleAtTargetRate = static_cast<juce::int64>((trackStartTime.count() / 1000.0) * targetSampleRate);
            const auto effectiveDuration = mixTrack.getEffectiveDuration(trackInfo->duration);
            const auto durationSamples = static_cast<juce::int64>((effectiveDuration.count() / 1000.0) * targetSampleRate);
            endSampleAtTargetRate = startSampleAtTargetRate + durationSamples;

            // Store cueStart in SOURCE sample rate (reader's rate) for file read offset calculations
            cueStartSamples = static_cast<juce::int64>((mixTrack.cueStart.count() / 1000.0) * reader->sampleRate);

            // MP3 seeks are O(n) forward-decodes until the frame table is built; defer that
            // to a background warm-up. Other formats seek instantly. A reused reader is
            // already warmed, so leave it marked warmed.
            if (!didReuse && reader->getFormatName().containsIgnoreCase("mp3"))
                warmed.store(false, std::memory_order_release);

            return true;
        }

        void PlaybackTrackSource::warmSeekIndex(const std::atomic<bool> &stopFlag)
        {
            if (!reader || reader->lengthInSamples <= 1152)
            {
                warmed.store(true, std::memory_order_release);
                return;
            }

            const auto startMs = juce::Time::getMillisecondCounterHiRes();

            // Reading at an advancing position drives the same forward frame scan the audio
            // thread would otherwise hit on the first seek - but off the realtime thread. Small
            // (~10s) chunks keep a stop request (play pressed, state teardown) responsive: a
            // pending stop is noticed within roughly one chunk's decode.
            juce::AudioBuffer<float> scratch(std::max(1, static_cast<int>(reader->numChannels)), 1152);
            const juce::int64 len = reader->lengthInSamples;
            const juce::int64 step = std::max<juce::int64>(1, static_cast<juce::int64>(reader->sampleRate) * 10);

            for (juce::int64 pos = step; pos < len; pos += step)
            {
                if (stopFlag.load(std::memory_order_acquire))
                    return;  // aborted; leave warmed=false, this reader won't be reused
                reader->read(&scratch, 0, 1152, pos, true, true);
            }
            if (!stopFlag.load(std::memory_order_acquire))
                reader->read(&scratch, 0, 1152, len - 1152, true, true);

            warmed.store(true, std::memory_order_release);
            spdlog::info("[MP3-WARM] Built seek index for track {} ({} samples) in {:.1f} ms",
                        trackId, len, juce::Time::getMillisecondCounterHiRes() - startMs);
        }

        MixPlaybackEngine::MixPlaybackEngine()
        {
            m_formatManager.registerBasicFormats();
            m_isPaused = true;
        }

        MixPlaybackEngine::~MixPlaybackEngine()
        {
            unloadMix();
            // m_garbage is cleared automatically by vector destructor, destroying any remaining states
        }

        bool MixPlaybackEngine::loadMix(MixProjectLoader *mixLoader)
        {
            if (!mixLoader)
            {
                spdlog::error("[PlaybackEngine] loadMix -> mixLoader is null");
                return false;
            }

            m_mixLoader = mixLoader;

            // Build new PlaybackState (shared_ptr)
            auto newState = buildPlaybackState(mixLoader);
            if (!newState)
            {
                spdlog::error("[PlaybackEngine] loadMix -> Failed to build PlaybackState");
                return false;
            }

            // Atomic swap - replace current state with new state
            auto oldState = m_currentPlaybackState.exchange(newState);

            // Add old state to garbage for deferred deletion
            if (oldState)
            {
                m_garbage.push_back(oldState);
            }

            // Attempt to collect garbage (free states that audio thread is done with)
            collectGarbage();

            spdlog::info("[PlaybackEngine] loadMix -> Exit (success)");
            return true;
        }

        void MixPlaybackEngine::unloadMix()
        {
            const juce::ScopedLock lock(m_critSec);
            unloadMixInternal();
        }

        void MixPlaybackEngine::unloadMixInternal()
        {
            spdlog::debug("[PlaybackEngine] unloadMixInternal -> Entry");
            m_isPaused = true;

            // Atomic swap to null
            auto oldState = m_currentPlaybackState.exchange(std::shared_ptr<PlaybackState>{nullptr});
            if (oldState)
            {
                m_garbage.push_back(oldState);
            }

            // Collect garbage
            collectGarbage();

            m_mixLoader = nullptr;
            m_currentPositionSamples = 0;

            spdlog::debug("[PlaybackEngine] unloadMixInternal -> Exit");
        }
        
        void MixPlaybackEngine::recalculateTrackPositions()
        {
            if (!m_mixLoader)
            {
                return;
            }

            const juce::ScopedLock lock(m_critSec);

            const auto currentPos = getPosition();

            // Rebuild PlaybackState with updated track positions
            auto newState = buildPlaybackState(m_mixLoader);
            if (newState)
            {
                auto oldState = m_currentPlaybackState.exchange(newState);
                if (oldState)
                {
                    m_garbage.push_back(oldState);
                }
                collectGarbage();
            }

            setPositionInternal(currentPos);
        }

        void MixPlaybackEngine::setPositionInternal(Duration_t positionMs)
        {
            if (!m_mixLoader)
            {
                return;
            }

            auto state = m_currentPlaybackState.load();
            if (!state)
            {
                return;
            }

            Duration_t clampedPosition = std::max(Duration_t{0}, std::min(positionMs, state->totalDuration));

            juce::int64 positionSamples = static_cast<juce::int64>((clampedPosition.count() / 1000.0) * m_sampleRate);

            m_currentPositionSamples = positionSamples;

            // Reposition all track sources using PlaybackState
            for (size_t i = 0; i < state->trackSources.size(); ++i)
            {
                auto &source = state->trackSources[i];

                if (source->mixTrackIndex >= state->trackStartTimes.size())
                {
                    continue;
                }

                Duration_t trackStartMs = state->trackStartTimes[source->mixTrackIndex];
                juce::int64 trackStartSamples = static_cast<juce::int64>((trackStartMs.count() / 1000.0) * m_sampleRate);

                if (positionSamples >= trackStartSamples)
                {
                    juce::int64 positionInTrack = positionSamples - trackStartSamples;

                    juce::int64 positionInSourceSamples = 0;
                    if (source->reader)
                    {
                        // Account for cueStart when calculating file position
                        positionInSourceSamples = static_cast<juce::int64>(positionInTrack * source->reader->sampleRate / m_sampleRate);
                        positionInSourceSamples = std::max(juce::int64(0), positionInSourceSamples + source->cueStartSamples);
                    }

                    source->currentPositionInSourceSamples = positionInSourceSamples;

                    if (source->readerSource)
                    {
                        source->readerSource->setNextReadPosition(positionInSourceSamples);
                    }
                    // Note: ResamplingAudioSource's internal buffer is not reset here.
                    // prepareToPlay() would reset it but is not RT-safe (allocates).
                    // This may cause brief interpolation artifacts (~one block) on seek.
                    // If testers flag this, consider moving seek handling to UI thread.
                }
                else
                {
                    // Before this track starts - position at cueStart (or 0 if negative cueStart)
                    juce::int64 initialPosition = std::max(juce::int64(0), source->cueStartSamples);
                    source->currentPositionInSourceSamples = initialPosition;
                    if (source->readerSource)
                    {
                        source->readerSource->setNextReadPosition(initialPosition);
                    }
                }
            }
        }
        
        void MixPlaybackEngine::setPosition(Duration_t positionMs)
        {
            // Lock-free seek: just set atomic flags, audio thread will apply the seek
            m_targetPositionMs = positionMs.count();
            m_pendingSeek = true;
        }

        Duration_t MixPlaybackEngine::getPosition() const
        {
            double positionSeconds = m_currentPositionSamples.load() / m_sampleRate;
            return Duration_t{static_cast<int64_t>(positionSeconds * 1000.0)};
        }

        void MixPlaybackEngine::setPaused(bool shouldPause)
        {
            spdlog::debug("MixPlaybackEngine::setPaused -> {}", shouldPause);

            // Warm-up runs only while paused and must not touch a reader once the audio
            // thread does. Stop and join it before un-pausing so the two never overlap.
            if (!shouldPause)
            {
                if (auto state = m_currentPlaybackState.load())
                    state->stopWarm();
            }

            m_isPaused = shouldPause;
        }

        void MixPlaybackEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
        {
            const juce::ScopedLock lock(m_critSec);

            auto currentPos = m_currentPositionSamples.load();

            m_sampleRate = sampleRate;
            m_blockSize = samplesPerBlockExpected;
            m_isPrepared = true;

            // Pre-allocate scratch buffer for audio callback (stereo, block size)
            // Note: getNextAudioBlock handles larger blocks by chunking, but we allocate
            // the expected size here. The chunking ensures we never exceed this.
            m_scratchBuffer.setSize(2, samplesPerBlockExpected);

            if (m_mixLoader)
            {
                // Rebuild PlaybackState now that we're prepared (creates track sources)
                auto newState = buildPlaybackState(m_mixLoader);
                if (newState)
                {
                    auto oldState = m_currentPlaybackState.exchange(newState);
                    if (oldState)
                    {
                        m_garbage.push_back(oldState);
                    }
                    collectGarbage();
                }

                if (currentPos > 0)
                {
                    m_currentPositionSamples = currentPos;
                }
            }
        }

        void MixPlaybackEngine::releaseResources()
        {
            const juce::ScopedLock lock(m_critSec);

            auto state = m_currentPlaybackState.load();
            if (state)
            {
                for (auto &source : state->trackSources)
                {
                    if (source && source->getAudioSource())
                    {
                        source->getAudioSource()->releaseResources();
                    }
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

            // Atomic load gets a shared_ptr, incrementing refcount.
            // This prevents deletion even if main thread swaps it out.
            auto state = m_currentPlaybackState.load();
            if (!state)
            {
                bufferToFill.clearActiveBufferRegion();
                return;
            }

            bufferToFill.clearActiveBufferRegion();

            juce::int64 startSample = m_currentPositionSamples.load();

            // SAFETY: Process in chunks if host requests more samples than our scratch buffer can handle.
            // This avoids buffer overflow while remaining fully real-time safe (no allocations).
            const int totalSamples = bufferToFill.numSamples;
            int samplesRemaining = totalSamples;
            int outputOffset = 0;

            // Defensive check: prevent infinite loop if m_blockSize is somehow invalid
            if (m_blockSize <= 0)
            {
                return;
            }

            while (samplesRemaining > 0)
            {
                const int chunkSize = std::min(samplesRemaining, m_blockSize);

                // Create a sub-buffer view for this chunk using raw pointers
                // Cap to stereo - this engine only supports up to 2 channels
                const int numChannels = std::min(bufferToFill.buffer->getNumChannels(), 2);
                float* channelPtrs[2];
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    channelPtrs[ch] = bufferToFill.buffer->getWritePointer(ch) + bufferToFill.startSample + outputOffset;
                }
                juce::AudioBuffer<float> chunkBuffer(channelPtrs, numChannels, chunkSize);

                mixActiveTracksForBlock(state, chunkBuffer, startSample, chunkSize);

                startSample += chunkSize;
                outputOffset += chunkSize;
                samplesRemaining -= chunkSize;
            }

            // Note: Visualizer FIFO is now fed by PlaybackController after EQ/Reverb
            // (unified tap point for both single-track and mix playback)

            // Update position using the already-incremented startSample (cleaner than re-loading atomic)
            m_currentPositionSamples = startSample;

            Duration_t currentPosMs = getPosition();
            Duration_t totalDuration = state->totalDuration;
            if (currentPosMs >= totalDuration)
            {
                m_currentPositionSamples = static_cast<juce::int64>((totalDuration.count() / 1000.0) * m_sampleRate);
            }

            // Check for pending seek and apply it (lock-free)
            if (m_pendingSeek.load())
            {
                Duration_t targetPos{m_targetPositionMs.load()};
                const juce::ScopedLock lock(m_critSec);  // Safe here - we're in audio thread, no contention
                setPositionInternal(targetPos);
                m_pendingSeek = false;
            }
        }

        void MixPlaybackEngine::mixActiveTracksForBlock(const std::shared_ptr<PlaybackState>& state, juce::AudioBuffer<float> &buffer, juce::int64 startSample, int numSamples)
        {
            // CRITICAL: Audio callback - NO LOCKS, NO ALLOCATION, NO LOGGING
            // State is passed from caller to avoid redundant atomic load and ensure consistency

            if (!state)
            {
                return;  // No mix loaded
            }

            const int numChannels = buffer.getNumChannels();

            // Use state->trackSources (safe: shared_ptr keeps state alive for the duration of this callback)
            for (size_t i = 0; i < state->trackSources.size(); ++i)
            {
                auto &source = state->trackSources[i];

                if (!source || !source->trackInfo || !source->reader)
                {
                    continue;
                }

                const MixTrack &mixTrack = source->mixTrack;

                // Use pre-calculated sample positions (avoids per-block ms-to-samples math)
                const auto trackStartSamples = source->startSampleAtTargetRate;
                const auto trackEndSamples = source->endSampleAtTargetRate;
                const auto trackDurationSamples = trackEndSamples - trackStartSamples;

                const auto blockEndSample = startSample + numSamples;

                if (trackEndSamples <= startSample || trackStartSamples >= blockEndSample)
                {
                    continue;
                }

                const auto trackReadStart = std::max(startSample - trackStartSamples, juce::int64(0));
                const auto trackReadEnd = std::min(blockEndSample - trackStartSamples, trackDurationSamples);
                int samplesToRead = static_cast<int>(trackReadEnd - trackReadStart);

                if (samplesToRead <= 0)
                    continue;

                int outputOffset = static_cast<int>(std::max(trackStartSamples - startSample, juce::int64(0)));

                // Handle pre-silence region (negative cueStart)
                // cueStartSamples is in source rate; convert to target rate for comparison
                juce::int64 preSilenceSamplesAtTargetRate = 0;
                if (source->cueStartSamples < 0 && source->reader)
                {
                    preSilenceSamplesAtTargetRate = static_cast<juce::int64>(
                        (-source->cueStartSamples) * m_sampleRate / source->reader->sampleRate);
                }

                // Calculate how much of this block is pre-silence vs actual audio
                int silenceSamples = 0;
                int audioSamples = samplesToRead;
                if (preSilenceSamplesAtTargetRate > 0 && trackReadStart < preSilenceSamplesAtTargetRate)
                {
                    juce::int64 silenceEnd = std::min(preSilenceSamplesAtTargetRate, trackReadStart + samplesToRead);
                    silenceSamples = static_cast<int>(silenceEnd - trackReadStart);
                    audioSamples = samplesToRead - silenceSamples;
                }

                // Use pre-allocated scratch buffer - just clear the portion we need (no resize)
                const int sourceChannels = source->reader ? source->reader->numChannels : numChannels;
                m_scratchBuffer.clear(0, samplesToRead);

                // Only read audio if we have non-silence samples
                if (audioSamples > 0)
                {
                    // Read into scratch buffer starting after the silence portion
                    juce::AudioSourceChannelInfo trackInfo(&m_scratchBuffer, silenceSamples, audioSamples);

                    if (auto *audioSource = source->getAudioSource())
                    {
                        audioSource->getNextAudioBlock(trackInfo);

                        // Handle mono-to-stereo conversion if needed
                        if (sourceChannels == 1 && numChannels == 2) {
                            // Source is mono but output is stereo - duplicate mono channel to right channel
                            const float* monoIn = m_scratchBuffer.getReadPointer(0) + silenceSamples;
                            float* rightOut = m_scratchBuffer.getWritePointer(1) + silenceSamples;
                            for (int sample = 0; sample < audioSamples; ++sample) {
                                rightOut[sample] = monoIn[sample];
                            }
                        }
                    }
                }

                // Get raw pointers for efficient sample access (avoids per-sample bounds checking)
                const float masterGain = m_masterGain.load();
                const float trackGain = masterGain * mixTrack.gainAdjustment;
                float* outLeft = buffer.getWritePointer(0);
                float* outRight = (numChannels > 1) ? buffer.getWritePointer(1) : nullptr;
                const float* srcLeft = m_scratchBuffer.getReadPointer(0);
                const float* srcRight = (numChannels > 1) ? m_scratchBuffer.getReadPointer(1) : nullptr;

                // Calculate envelope gain at block boundaries (avoids per-sample envelope calls)
                Duration_t startTimeInTrack{static_cast<int64_t>((trackReadStart * 1000.0) / m_sampleRate)};
                Duration_t endTimeInTrack{static_cast<int64_t>(((trackReadStart + samplesToRead - 1) * 1000.0) / m_sampleRate)};
                const float startEnvGain = getEnvelopeGainForTrack(mixTrack, startTimeInTrack);
                const float endEnvGain = getEnvelopeGainForTrack(mixTrack, endTimeInTrack);

                // Check if gain is constant across the block (within tolerance)
                const bool constantGain = std::abs(startEnvGain - endEnvGain) < 0.0001f;

                if (constantGain)
                {
                    // Constant gain - simple multiply-add
                    const float gain = startEnvGain * trackGain;
                    for (int sample = 0; sample < samplesToRead; ++sample)
                    {
                        int outputSample = outputOffset + sample;
                        if (outputSample >= 0 && outputSample < numSamples)
                        {
                            outLeft[outputSample] += srcLeft[sample] * gain;
                            if (outRight && srcRight) //-V1051
                            {
                                outRight[outputSample] += srcRight[sample] * gain;
                            }
                        }
                    }
                }
                else
                {
                    // Gain is changing (fade) - use linear interpolation
                    const float gainDelta = (endEnvGain - startEnvGain) / static_cast<float>(samplesToRead - 1);
                    float envGain = startEnvGain;
                    for (int sample = 0; sample < samplesToRead; ++sample)
                    {
                        const float gain = envGain * trackGain;
                        int outputSample = outputOffset + sample;
                        if (outputSample >= 0 && outputSample < numSamples)
                        {
                            outLeft[outputSample] += srcLeft[sample] * gain;
                            if (outRight && srcRight) //-V1051
                            {
                                outRight[outputSample] += srcRight[sample] * gain;
                            }
                        }
                        envGain += gainDelta;
                    }
                }
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
