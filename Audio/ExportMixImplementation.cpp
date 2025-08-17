/*
  ==============================================================================

    MixExporter.cpp
    Created: 1 Jun 2025 8:08:15pm
    Author:  GersonKurz

  ==============================================================================
*/

#include <Audio/ExportMixImplementation.h>
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
              m_totalMixDurationMs{Duration_t::zero()}
        {
            if (m_progressCallback)
                m_progressCallback(0.0f, "Starting export...");

            loadMix(mixId);
        }

        bool ExportMixImplementation::run()
        {
            typedef bool (ExportMixImplementation::*OperationStep)();
            struct OperationDefinition
            {
                std::string name;
                OperationStep step;
            };

            const std::vector<OperationDefinition> steps{
                {"Calculate Mix Duration", &ExportMixImplementation::calculateMixDuration},
                {"Calculate Total Output Samples", &ExportMixImplementation::calculateTotalOutputSamples},
                {"Setup Juce AudioFormatManage & Writer", &ExportMixImplementation::setupAudioFormatManagerAndWriter},
                {"Preparing active track sources", &ExportMixImplementation::prepareActiveTrackSources},
                {"Run Mixing Loop", &ExportMixImplementation::runMixingLoop},
            };

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
                    if (m_progressCallback)
                        m_progressCallback(1.0f, "Error: " + opdef.name);
                    return fail(std::format("Operation '{}' failed after {} ms ({}).", opdef.name, duration, durationToString((Duration_t)duration)));
                }
                else
                {
                    spdlog::info("MTE: Operation '{}' completed successfully in {} ms ({}).", opdef.name, duration, durationToString((Duration_t)duration));
                }
            }
            return true;
        }
        void ExportMixImplementation::calculateTrackPositions()
        {
            m_trackPositions.clear();
            m_trackPositions.resize(m_mixTracks.size());
            Duration_t previousTrackStart{0};
            
            for (size_t i = 0; i < m_mixTracks.size(); ++i)
            {
                const auto& mixTrack = m_mixTracks[i];
                const auto* trackInfo = getTrackInfoForId(mixTrack.trackId);
                if (!trackInfo)
                {
                    spdlog::warn("Track info not found for track ID {} during position calculation", mixTrack.trackId);
                    continue;
                }
                
                Duration_t trackStart{0};
                
                if (i == 0)
                {
                    // First track starts at position 0
                    trackStart = Duration_t{0};
                }
                else
                {
                    // ATTACH formula: Next track start = Previous track start + Previous track's attachTo - Current track's attachFrom
                    const auto& prevTrack = m_mixTracks[i-1];
                    trackStart = previousTrackStart + prevTrack.attachTo - mixTrack.attachFrom;
                }
                
                // Store the calculated position
                TrackTimelinePosition pos;
                pos.startTime = trackStart;
                pos.endTime = trackStart + trackInfo->duration;
                m_trackPositions[i] = pos;
                
                // Remember this track's start for the next iteration
                previousTrackStart = trackStart;
                
                spdlog::debug("Track at index {} (ID {}) positioned at [{}, {}]", 
                    i,
                    mixTrack.trackId, 
                    durationToString(pos.startTime), 
                    durationToString(pos.endTime));
            }
        }
        
        float interpolateVolumeFromEnvelope(const std::vector<EnvelopePoint> &envelopePoints, Duration_t timeInTrack)
        {
            if (envelopePoints.empty())
            {
                return 1.0f; // Default to full volume if no envelope points
            }

            // If before first point, use first point's volume
            if (timeInTrack <= envelopePoints.front().time)
            {
                return envelopePoints.front().volume / (float)VOLUME_NORMALIZATION;
            }

            // If after last point, use last point's volume
            if (timeInTrack >= envelopePoints.back().time)
            {
                return envelopePoints.back().volume / (float)VOLUME_NORMALIZATION;
            }

            // Find the two points to interpolate between
            for (size_t i = 0; i < envelopePoints.size() - 1; ++i)
            {
                const auto &pointA = envelopePoints[i];
                const auto &pointB = envelopePoints[i + 1];

                if (timeInTrack >= pointA.time && timeInTrack <= pointB.time)
                {
                    // Linear interpolation between the two points
                    float progress = 0.0f;
                    auto timeDiff = pointB.time - pointA.time;
                    if (timeDiff.count() > 0)
                    {
                        progress = (float)(timeInTrack - pointA.time).count() / (float)timeDiff.count();
                    }

                    float volumeA = pointA.volume / (float)VOLUME_NORMALIZATION;
                    float volumeB = pointB.volume / (float)VOLUME_NORMALIZATION;

                    return volumeA + progress * (volumeB - volumeA);
                }
            }

            // Fallback (shouldn't reach here)
            return 1.0f;
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

            // Find the maximum end time among all tracks
            Duration_t maxEndTime{0};
            for (const auto& pos : m_trackPositions)
            {
                if (pos.endTime > maxEndTime)
                {
                    maxEndTime = pos.endTime;
                }
            }

            m_totalMixDurationMs = maxEndTime;

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

                // Apply per-track gain adjustment
                envelopeGain *= mixTrackDef.gainAdjustment;

                // Clamp gain to reasonable bounds
                envelopeGain = juce::jlimit(0.0f, 1.0f, envelopeGain); // Clamp after applying gainAdjustment

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

                    for (unsigned int chan = 0; chan < outputNumChannels(); ++chan)
                    {
                        masterOutputBlock.addSample(chan, (int)targetSampleInMasterBlock, sourceTrackBlock.getSample(chan, (int)s_idx_in_block) * envelopeGain);
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
            // Get the source and position data using the track's index in the mix
            ActiveTrackSource& activeSource = m_activeSources[trackIndex];
            const TrackTimelinePosition& trackPos = m_trackPositions[trackIndex];

            const MixTrack &mixTrackDef = *activeSource.mixTrackDefPtr;
            const TrackInfo &trackInfo = *activeSource.trackInfoPtr;
            juce::AudioFormatReader *reader = activeSource.reader.get();

            // --- Calculate timing for *this specific track* based on calculated positions ---
            juce::int64 trackMixStartSamples = static_cast<juce::int64>((trackPos.startTime.count() / 1000.0) * outputSampleRate());

            // With envelope system, tracks play their full duration
            Duration_t trackFileEffectiveDurationMs = trackInfo.duration;
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
            juce::AudioBuffer<float> tempReadBuffer(readerChannels, (int)overallContext.samplesToProcessInThisBlock);
            tempReadBuffer.clear();
            
            // This will be our final buffer with the output channel count
            juce::AudioBuffer<float> sourceTrackBlock(outputNumChannels(), (int)overallContext.samplesToProcessInThisBlock);
            sourceTrackBlock.clear();

            juce::int64 readStartInOutputTimeline = juce::jmax(overallContext.currentBlockStartTimeSamples, trackMixStartSamples);
            juce::int64 readEndInOutputTimeline = juce::jmin(overallContext.currentBlockEndTimeSamples, trackMixEndSamples);
            juce::int64 numSamplesToReadForThisTrackInBlock = readEndInOutputTimeline - readStartInOutputTimeline;

            if (numSamplesToReadForThisTrackInBlock <= 0)
            {
                spdlog::warn("Track ID {} has no samples to read in current block ({} - {}). Skipping.", mixTrackDef.trackId,
                             overallContext.currentBlockStartTimeSamples, overallContext.currentBlockEndTimeSamples);
                return true;
            }

            // With envelope system, we start reading from the beginning of the track file
            // The envelope controls volume, not which part of the file to read
            juce::int64 readOffsetInSourceFileSamples = (readStartInOutputTimeline - trackMixStartSamples) * reader->sampleRate / outputSampleRate();

            // Read into temp buffer with proper channel count
            const auto readSuccess = reader->read(&tempReadBuffer, 0, (int)numSamplesToReadForThisTrackInBlock, readOffsetInSourceFileSamples, true, readerChannels > 1);
            
            if (readSuccess)
            {
                // Copy/convert from temp buffer to output format
                if (readerChannels == 1 && outputNumChannels() == 2)
                {
                    // Mono to stereo: duplicate the mono channel
                    const auto* monoData = tempReadBuffer.getReadPointer(0);
                    sourceTrackBlock.copyFrom(0, 0, monoData, (int)numSamplesToReadForThisTrackInBlock);
                    sourceTrackBlock.copyFrom(1, 0, monoData, (int)numSamplesToReadForThisTrackInBlock);
                }
                else if (readerChannels == 2 && outputNumChannels() == 2)
                {
                    // Stereo to stereo: direct copy
                    sourceTrackBlock.copyFrom(0, 0, tempReadBuffer.getReadPointer(0), (int)numSamplesToReadForThisTrackInBlock);
                    sourceTrackBlock.copyFrom(1, 0, tempReadBuffer.getReadPointer(1), (int)numSamplesToReadForThisTrackInBlock);
                }
                else if (readerChannels == 2 && outputNumChannels() == 1)
                {
                    // Stereo to mono: mix down
                    const auto* leftData = tempReadBuffer.getReadPointer(0);
                    const auto* rightData = tempReadBuffer.getReadPointer(1);
                    auto* monoOut = sourceTrackBlock.getWritePointer(0);
                    for (int i = 0; i < (int)numSamplesToReadForThisTrackInBlock; ++i)
                    {
                        monoOut[i] = (leftData[i] + rightData[i]) * 0.5f;
                    }
                }
                else if (readerChannels == 1 && outputNumChannels() == 1)
                {
                    // Mono to mono: direct copy
                    sourceTrackBlock.copyFrom(0, 0, tempReadBuffer.getReadPointer(0), (int)numSamplesToReadForThisTrackInBlock);
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

            return applyMixTrackSpecs(mixTrackDef, trackContext, masterOutputBlock, sourceTrackBlock);
        }

    } // namespace audio
} // namespace jucyaudio
