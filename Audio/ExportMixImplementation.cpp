/*
  ==============================================================================

    MixExporter.cpp
    Created: 1 Jun 2025 8:08:15pm
    Author:  GersonKurz

  ==============================================================================
*/

#include <Audio/ExportMixImplementation.h>
#include <Audio/AudioUtils.h>
#include <Utils/AssortedUtils.h>
#include <Database/Includes/Constants.h>

namespace jucyaudio
{
    namespace audio
    {
        using namespace database;

        ActiveTrackSource::ActiveTrackSource(TrackId id, const TrackInfo *ti, const MixTrack *mtd, juce::AudioFormatManager &formatManager,
                                             [[maybe_unused]] double targetSampleRate, [[maybe_unused]] int targetNumChannels)
            : trackId{id},
              trackInfoPtr{ti},
              mixTrackDefPtr{mtd}
        {
            const auto trackPath{trackInfoPtr->reconstructFullPath()};
            juce::File sourceFile{pathToString(trackPath)};
            reader.reset(formatManager.createReaderFor(sourceFile));
            if (reader)
            {
                readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader.get(), false /*don't delete reader*/);

                // TODO: Setup resampling if reader->sampleRate != targetSampleRate or reader->numChannels != targetNumChannels
                // if (reader->sampleRate != targetSampleRate || reader->numChannels != targetNumChannels) {
                //    resamplerSource = std::make_unique<juce::ResamplingAudioSource>(readerSource.get(), false, targetNumChannels);
                //    resamplerSource->setResamplingRatio(reader->sampleRate / targetSampleRate);
                //    // The 'source' to call getNextAudioBlock on would then be resamplerSource.get()
                // }
                // For now, assume readerSource is what we use.
                // Prepare readerSource for reading
                // readerSource->prepareToPlay(processingBlockSize, targetSampleRate); // Example block size
            }
            else
            {
                spdlog::error("MTE: Failed to create reader for track ID {} ({})", id, pathToString(trackPath));
            }
        }

        ExportMixImplementation::ExportMixImplementation(MixId mixId, const ActiveExportSettings &settings,
                                                         MixExporterProgressCallback progressCallback)
            : MixProjectLoader{},
              m_progressCallback{progressCallback},
              m_settings{settings},
              m_totalMixDurationMs{Duration_t::zero()},
              m_totalOutputSamples{0}
        {
            if (m_progressCallback)
                m_progressCallback(0.0f, "Starting export...");

            loadMix(mixId);
        }

        ExportResult ExportMixImplementation::run()
        {
            using OperationStep = bool (ExportMixImplementation::*)();

            struct OperationDefinition
            {
                std::string_view name;
                OperationStep step;
            };

            const std::array<OperationDefinition, 5> steps{{
                {"Calculate Mix Duration", &ExportMixImplementation::calculateMixDuration},
                {"Calculate Total Output Samples", &ExportMixImplementation::calculateTotalOutputSamples},
                {"Setup Juce AudioFormatManage & Writer", &ExportMixImplementation::setupAudioFormatManagerAndWriter},
                {"Preparing active track sources", &ExportMixImplementation::prepareActiveTrackSources},
                {"Run Mixing Loop", &ExportMixImplementation::runMixingLoop},
            }};

            using clock = std::chrono::steady_clock;
            for (const auto &opdef : steps)
            {
                const auto start = clock::now();
                spdlog::info("MTE: beginning operation '{}'", opdef.name);
                bool success = (this->*(opdef.step))();
                const auto end = clock::now();
                const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                if (!success)
                {
                    auto errorMsg = std::format("Operation '{}' failed after {} ms ({}).", opdef.name, duration, durationToString((Duration_t)duration));
                    if (m_progressCallback)
                        m_progressCallback(1.0f, "Error: " + errorMsg);
                    fail(errorMsg);
                    return ExportResult::Failure(errorMsg);
                }
                else
                {
                    spdlog::info("MTE: Operation '{}' completed successfully in {} ms ({}).", opdef.name, duration, durationToString((Duration_t)duration));
                }
            }
            return ExportResult::Success(static_cast<int>(m_failedTracks.size()));
        }
        void ExportMixImplementation::calculateTrackPositions()
        {
            m_trackPositions.clear();
            m_trackPositions.resize(m_mixTracks.size());

            // Through the shared walk in MixInfo.h. Every row is positioned, including one whose track
            // cannot be resolved: its position comes from the mix's own attach points, so it holds its
            // place rather than pulling everything after it earlier.
            //
            // Not because this exporter ever renders such a mix - prepareActiveTrackSources refuses
            // the whole export when a row has no TrackInfo, so the unresolved case never reaches the
            // renderer. It is so that the total this class reports, the length the editor shows, the
            // M3U export, playback and the timeline are one answer computed once, rather than five
            // that happen to agree while every track resolves.
            const auto starts{database::calculateMixTrackStarts(m_mixTracks)};

            for (size_t i = 0; i < m_mixTracks.size(); ++i)
            {
                const auto &mixTrack = m_mixTracks[i];
                const auto *trackInfo = getTrackInfoForId(mixTrack.trackId);

                TrackTimelinePosition pos;
                pos.startTime = starts[i];

                if (trackInfo)
                {
                    pos.resolved = true;
                    pos.endTime = pos.startTime + mixTrack.getEffectiveDuration(trackInfo->duration);
                }
                else
                {
                    // No audio to place, so nothing to end. Left unresolved rather than given a
                    // zero-length end, because the mix's total is the maximum of these and a row that
                    // sits past the last audible sample would stretch it.
                    spdlog::warn("Track info not found for track ID {} during position calculation", mixTrack.trackId);
                    pos.endTime = pos.startTime;
                }

                m_trackPositions[i] = pos;

                spdlog::debug("Track at index {} (ID {}) positioned at [{}, {}]{}",
                    i,
                    mixTrack.trackId,
                    durationToString(pos.startTime),
                    durationToString(pos.endTime),
                    pos.resolved ? "" : " (unresolved)");
            }
        }

        bool ExportMixImplementation::calculateMixDuration()
        {
            assert(m_totalMixDurationMs == Duration_t::zero());
            if (m_mixTracks.empty())
            {
                return fail("MTE: No mix tracks found for mix ID " + std::to_string(m_mixId));
            }

            // Calculate timeline positions for all tracks using ATTACH model
            calculateTrackPositions();

            // The total through the shared walk rather than a maximum over the positions above. Same
            // rule, one implementation - and the maximum would have had to know to ignore a row that
            // could not be resolved, whose endTime is only a placeholder.
            m_totalMixDurationMs = database::calculateMixDuration(m_mixTracks,
                [this](TrackId trackId) -> std::optional<Duration_t>
                {
                    const auto *trackInfo = getTrackInfoForId(trackId);
                    if (!trackInfo)
                    {
                        return std::nullopt;
                    }
                    return trackInfo->duration;
                });

            if (m_totalMixDurationMs <= Duration_t::zero())
            {
                return fail("MTE: Total mix duration is zero or negative for mix ID " + std::to_string(m_mixId));
            }
            spdlog::info("MTE: Total mix duration is {} ms ({})", m_totalMixDurationMs.count(), durationToString(m_totalMixDurationMs));
            return true;
        }

        // @brief Calculate the total number of output samples based on the mix duration and output sample rate.
        // Analyze tracks to determine actual max SR/Channels or if resampling is needed.
        // For now, we'll assume sources will be resampled/mixed to outputSampleRate()/outputNumChannels() by JUCE if needed.
        bool ExportMixImplementation::calculateTotalOutputSamples()
        {
            m_totalOutputSamples = static_cast<juce::int64>((m_totalMixDurationMs.count() / 1000.0) * outputSampleRate());
            spdlog::info("MTE: Total estimated output samples: {}", m_totalOutputSamples);
            return true;
        }

        // @brief Setup the JUCE AudioFormatManager and create an AudioFormatWriter for the target file.
        bool ExportMixImplementation::setupAudioFormatManagerAndWriter()
        {
            return onSetupAudioFormatManagerAndWriter();
        }

        // @brief This is more complex than the A/B stitch. We need to manage multiple sources
        // potentially active at the same time, placed correctly on the output timeline.
        bool ExportMixImplementation::runMixingLoop()
        {
            return onRunMixingLoop();
        }

        // @brief Apply MixTrack's internal fades, volume, and crossfade logic
        bool ExportMixImplementation::applyMixTrackSpecs(const MixTrack &mixTrackDef, const SampleContext &context, juce::AudioBuffer<float> &masterOutputBlock,
                                                         const juce::AudioBuffer<float> &sourceTrackBlock)
        {
            for (juce::int64 s_idx_in_block = 0; s_idx_in_block < context.numSamplesToReadFromSource; ++s_idx_in_block)
            {
                const juce::int64 currentSampleInOutputTimeline = context.readStartInOutputTimeline + s_idx_in_block;
                const juce::int64 currentSampleInThisMixTrack =
                    currentSampleInOutputTimeline - context.trackMixStartSamples; // 0 to trackFileEffectiveDurationSamples

                // Convert sample position back to time within the track
                Duration_t timeInTrack{static_cast<int64_t>((currentSampleInThisMixTrack * 1000.0) / outputSampleRate())};

                // Get volume from envelope interpolation
                float envelopeGain = interpolateVolumeFromEnvelope(mixTrackDef.envelopePoints, timeInTrack);

                // Apply per-track gain adjustment.
                // Note: No clamping here - intentional design choice to match real-time playback.
                // Gains > 1.0 are allowed; clipping prevention is the user's responsibility.
                envelopeGain *= mixTrackDef.gainAdjustment;

                // Add to master output block
                const juce::int64 targetSampleInMasterBlock = currentSampleInOutputTimeline - context.currentBlockStartTimeSamples;
                if (targetSampleInMasterBlock >= 0 && targetSampleInMasterBlock < context.samplesToProcessInThisBlock)
                {
                    if (s_idx_in_block < 5 && context.samplesWrittenTotal < 8192)
                    {
                        spdlog::debug("MTE DEBUG: s_idx_in_block: {}, timeInTrack: {}ms, envelopeGain: {}", s_idx_in_block, timeInTrack.count(), envelopeGain);
                        if (sourceTrackBlock.getNumChannels() > 0)
                        {
                            spdlog::debug("MTE DEBUG: sourceSample[0]: {}", sourceTrackBlock.getSample(0, (int)s_idx_in_block));
                        }
                    }

                    // sourceTrackBlock was filled starting at destStartOffset in contributeFromActiveSource
                    // destStartOffset = readStartInOutputTimeline - currentBlockStartTimeSamples
                    for (unsigned int chan = 0; chan < outputNumChannels(); ++chan)
                    {
                        const int actualSourceIndex = (int)(context.readStartInOutputTimeline - context.currentBlockStartTimeSamples) + (int)s_idx_in_block;
                        masterOutputBlock.addSample(chan, (int)targetSampleInMasterBlock, sourceTrackBlock.getSample(chan, actualSourceIndex) * envelopeGain);
                    }
                }
            }
            return true; // Successfully processed this mix track
        }


        bool ExportMixImplementation::fail(const std::string &errorMessage)
        {
            spdlog::error("MTE: {}", errorMessage);
            if (m_progressCallback)
                m_progressCallback(1.0f, "Error: " + errorMessage);
            return false;
        }

        bool ExportMixImplementation::prepareActiveTrackSources()
        {
            m_activeSources.clear();
            for (const auto &mixTrackDef : m_mixTracks)
            {
                if (const auto trackInfo{getTrackInfoForId(mixTrackDef.trackId)})
                {
                    // m_activeSources.emplace_back(mixTrackDef.trackId, trackInfo, &mixTrackDef, m_formatManager, outputSampleRate(),
                    // outputNumChannels()); Better: use a map for easier lookup if needed, though iterating a vector is fine for a few tracks. For now,
                    // let's build a vector. The order might matter if we rely on previous.
                    m_activeSources.emplace_back(
                        ActiveTrackSource{mixTrackDef.trackId, trackInfo, &mixTrackDef, m_formatManager, outputSampleRate(), (int)outputNumChannels()});

                    if (!m_activeSources.back().reader)
                    { // Check if reader creation failed in constructor
                        return fail("MTE: Failed to prepare reader for track " + std::to_string(mixTrackDef.trackId));
                    }
                }
                else
                {
                    return fail("MTE: TrackInfo not found for track ID " + std::to_string(mixTrackDef.trackId) + " during source preparation.");
                }
            }
            // Optional: Prepare all sources for playback
            // for (auto& source : m_activeSources) {
            //     if (source.readerSource) { // And resampler if used
            //         source.readerSource->prepareToPlay(processingBlockSize, outputSampleRate());
            //     }
            // }
            return true;
        }

        // Renamed and takes an ActiveTrackSource
        bool ExportMixImplementation::contributeFromActiveSource(size_t trackIndex, const SampleContext &overallContext,
                                                                 juce::AudioBuffer<float> &masterOutputBlock)
        {
            // Get the source and position data using the track's index in the mix.
            //
            // The two are indexed alike only because prepareActiveTrackSources refuses the whole
            // export when any track cannot be resolved, so m_activeSources is either as long as
            // m_mixTracks or the export never reaches here. m_trackPositions holds every row,
            // resolved or not. Anything that makes the export tolerate an unresolvable track has to
            // give the sources their row index rather than rely on the two lining up.
            ActiveTrackSource& activeSource = m_activeSources[trackIndex];
            const TrackTimelinePosition& trackPos = m_trackPositions[trackIndex];

            const MixTrack &mixTrackDef = *activeSource.mixTrackDefPtr;
            const TrackInfo &trackInfo = *activeSource.trackInfoPtr;
            juce::AudioFormatReader *reader = activeSource.reader.get();

            // --- Calculate timing for *this specific track* based on calculated positions ---
            juce::int64 trackMixStartSamples = static_cast<juce::int64>((trackPos.startTime.count() / 1000.0) * outputSampleRate());

            // Use effective duration from cue points
            Duration_t trackFileEffectiveDurationMs = mixTrackDef.getEffectiveDuration(trackInfo.duration);
            const juce::int64 trackFileEffectiveDurationSamples =
                static_cast<juce::int64>((trackFileEffectiveDurationMs.count() / 1000.0) * outputSampleRate());
            juce::int64 trackMixEndSamples = trackMixStartSamples + trackFileEffectiveDurationSamples;

            // --- Check if this track is active in the current masterOutputBlock ---
            if (trackMixEndSamples <= overallContext.currentBlockStartTimeSamples || trackMixStartSamples >= overallContext.currentBlockEndTimeSamples)
            {
                return true; // Not active in this block
            }

            // --- Track is active: Use its pre-opened reader ---
            // Create buffer with reader's actual channel count first
            const int readerChannels = reader->numChannels;
            
            // Debug logging for mono tracks
            if (readerChannels == 1)
            {
                spdlog::debug("MTE: Processing MONO track ID {} - reader channels: {}, output channels: {}",
                    mixTrackDef.trackId, readerChannels, outputNumChannels());
            }

            juce::int64 readStartInOutputTimeline = juce::jmax(overallContext.currentBlockStartTimeSamples, trackMixStartSamples);
            juce::int64 readEndInOutputTimeline = juce::jmin(overallContext.currentBlockEndTimeSamples, trackMixEndSamples);
            juce::int64 numSamplesToReadForThisTrackInBlock = readEndInOutputTimeline - readStartInOutputTimeline;

            if (numSamplesToReadForThisTrackInBlock <= 0)
            {
                spdlog::warn("Track ID {} has no samples to read in current block ({} - {}). Skipping.", mixTrackDef.trackId,
                             overallContext.currentBlockStartTimeSamples, overallContext.currentBlockEndTimeSamples);
                return true;
            }
            
            // Create buffers with the ACTUAL number of samples we'll read, not the full block size
            // If the source sample rate is different, we need to adjust the number of samples to read
            juce::int64 samplesToReadFromFile = numSamplesToReadForThisTrackInBlock;
            constexpr double epsilon = 1e-9;
            if (std::abs(reader->sampleRate - outputSampleRate()) > epsilon)
            {
                // Adjust for sample rate difference
                samplesToReadFromFile = (numSamplesToReadForThisTrackInBlock * reader->sampleRate) / outputSampleRate();
            }
            
            // Resize reusable buffers only if needed (avoids per-block heap allocation)
            if (m_tempReadBuffer.getNumChannels() < readerChannels || m_tempReadBuffer.getNumSamples() < (int)samplesToReadFromFile)
            {
                m_tempReadBuffer.setSize(readerChannels, (int)samplesToReadFromFile, false, false, true);
            }
            m_tempReadBuffer.clear();

            // Resize output buffer only if needed
            const int neededSamples = (int)overallContext.samplesToProcessInThisBlock;
            if (m_sourceTrackBlock.getNumChannels() < (int)outputNumChannels() || m_sourceTrackBlock.getNumSamples() < neededSamples)
            {
                m_sourceTrackBlock.setSize(outputNumChannels(), neededSamples, false, false, true);
            }
            m_sourceTrackBlock.clear();

            // Account for cueStart when reading from file
            // If cueStart > 0, we skip that portion of the file
            // If cueStart < 0, we're in a pre-silence region (output silence before audio starts)
            juce::int64 positionWithinEffectiveRegion = (readStartInOutputTimeline - trackMixStartSamples) * reader->sampleRate / outputSampleRate();
            juce::int64 cueStartSamples = static_cast<juce::int64>((mixTrackDef.cueStart.count() / 1000.0) * reader->sampleRate);
            juce::int64 filePosition = positionWithinEffectiveRegion + cueStartSamples;

            // Handle pre-silence region (negative cueStart)
            juce::int64 preSilenceSamplesAtSourceRate = 0;
            juce::int64 actualSamplesToReadFromFile = samplesToReadFromFile;
            juce::int64 readOffsetInSourceFileSamples = filePosition;

            if (filePosition < 0)
            {
                preSilenceSamplesAtSourceRate = -filePosition;
                if (preSilenceSamplesAtSourceRate >= samplesToReadFromFile)
                {
                    // Entire block is pre-silence - buffer already cleared, skip to envelope/mixing
                    actualSamplesToReadFromFile = 0;
                    readOffsetInSourceFileSamples = 0;
                }
                else
                {
                    // Partial pre-silence: read fewer samples, starting from file position 0
                    actualSamplesToReadFromFile = samplesToReadFromFile - preSilenceSamplesAtSourceRate;
                    readOffsetInSourceFileSamples = 0;
                }
            }

            // Debug logging for mono tracks - show what we're reading
            int destStartOffset = (int)(readStartInOutputTimeline - overallContext.currentBlockStartTimeSamples);
            // Always log the first few blocks for mono tracks to debug the issue
            if (readerChannels == 1 && overallContext.currentBlockStartTimeSamples < 20000)
            {
                spdlog::info("MTE-MONO-DEBUG: track {} - readOffset={}, numSamples={}, destOffset={}, blockStart={}, trackStart={}, readerSR={}, outputSR={}", 
                    mixTrackDef.trackId, readOffsetInSourceFileSamples, numSamplesToReadForThisTrackInBlock, destStartOffset,
                    overallContext.currentBlockStartTimeSamples, trackMixStartSamples, reader->sampleRate, outputSampleRate());
            }

            // If entire block is pre-silence, skip reading
            if (actualSamplesToReadFromFile <= 0)
            {
                // Buffer already cleared - silence will be mixed. Skip to next track.
                return true;
            }

            // Calculate actual output samples (accounting for pre-silence and sample rate conversion)
            juce::int64 actualSamplesToOutputForThisTrack = numSamplesToReadForThisTrackInBlock;
            if (preSilenceSamplesAtSourceRate > 0)
            {
                juce::int64 preSilenceSamplesAtOutputRate = (preSilenceSamplesAtSourceRate * outputSampleRate()) / reader->sampleRate;
                destStartOffset += (int)preSilenceSamplesAtOutputRate;
                actualSamplesToOutputForThisTrack -= preSilenceSamplesAtOutputRate;
            }

            // Read into temp buffer with proper channel count
            // For mono files, we should only read the left channel (true, false)
            // For stereo files, we read both channels (true, true)
            const auto readSuccess = reader->read(&m_tempReadBuffer, 0, (int)actualSamplesToReadFromFile, readOffsetInSourceFileSamples, true, readerChannels > 1);
            
            if (readSuccess)
            {
                // If we need to resample, do it now
                juce::AudioBuffer<float> resampledBuffer;
                const float* sourceData0 = m_tempReadBuffer.getReadPointer(0);
                const float* sourceData1 = (readerChannels > 1) ? m_tempReadBuffer.getReadPointer(1) : nullptr;

                if (actualSamplesToReadFromFile != actualSamplesToOutputForThisTrack)
                {
                    // Need to resample - create a resampled buffer
                    resampledBuffer.setSize(readerChannels, (int)actualSamplesToOutputForThisTrack);

                    // Simple linear interpolation resampling
                    const double ratio = (double)actualSamplesToReadFromFile / (double)actualSamplesToOutputForThisTrack;

                    for (int ch = 0; ch < readerChannels; ++ch)
                    {
                        const float* src = m_tempReadBuffer.getReadPointer(ch);
                        float* dst = resampledBuffer.getWritePointer(ch);

                        for (int i = 0; i < actualSamplesToOutputForThisTrack; ++i)
                        {
                            const double srcPos = i * ratio;
                            const int srcIdx = (int)srcPos;
                            const float frac = (float)(srcPos - srcIdx);

                            if (srcIdx + 1 < actualSamplesToReadFromFile)
                            {
                                dst[i] = src[srcIdx] * (1.0f - frac) + src[srcIdx + 1] * frac;
                            }
                            else if (srcIdx < actualSamplesToReadFromFile)
                            {
                                dst[i] = src[srcIdx];
                            }
                            else
                            {
                                dst[i] = 0.0f;
                            }
                        }
                    }

                    // Update pointers to use resampled data
                    sourceData0 = resampledBuffer.getReadPointer(0);
                    sourceData1 = (readerChannels > 1) ? resampledBuffer.getReadPointer(1) : nullptr;
                }

                // Copy/convert from temp buffer to stereo output (outputNumChannels() is always 2)
                if (readerChannels == 1)
                {
                    // Mono to stereo: duplicate the mono channel
                    // Debug: Check if we have actual data
                    float maxSample = 0.0f;
                    for (int i = 0; i < std::min(100, (int)actualSamplesToOutputForThisTrack); ++i)
                    {
                        maxSample = std::max(maxSample, std::abs(sourceData0[i]));
                    }
                    spdlog::debug("MTE: Mono track {} - max sample in first 100: {}, copying {} samples to offset {}",
                        mixTrackDef.trackId, maxSample, actualSamplesToOutputForThisTrack, destStartOffset);

                    m_sourceTrackBlock.copyFrom(0, destStartOffset, sourceData0, (int)actualSamplesToOutputForThisTrack);
                    m_sourceTrackBlock.copyFrom(1, destStartOffset, sourceData0, (int)actualSamplesToOutputForThisTrack);
                }
                else // readerChannels == 2
                {
                    // Stereo to stereo: direct copy
                    m_sourceTrackBlock.copyFrom(0, destStartOffset, sourceData0, (int)actualSamplesToOutputForThisTrack);
                    m_sourceTrackBlock.copyFrom(1, destStartOffset, sourceData1, (int)actualSamplesToOutputForThisTrack);
                }
            }
            else
            {
                const auto trackPath{trackInfo.reconstructFullPath()};
                spdlog::error("MTE: Failed to read samples for track ID {} from source file: {}", mixTrackDef.trackId, pathToString(trackPath));
                return false;
            }

            // SampleContext for this specific track's contribution within the block
            SampleContext trackContext = overallContext;
            trackContext.numSamplesToReadFromSource = numSamplesToReadForThisTrackInBlock;
            trackContext.readStartInOutputTimeline = readStartInOutputTimeline;
            trackContext.trackMixStartSamples = trackMixStartSamples;
            trackContext.trackFileEffectiveDurationSamples = trackFileEffectiveDurationSamples;

            return applyMixTrackSpecs(mixTrackDef, trackContext, masterOutputBlock, m_sourceTrackBlock);
        }

    } // namespace audio
} // namespace jucyaudio
