/*
  ==============================================================================

    MixExporter.cpp
    Created: 1 Jun 2025 8:08:15pm
    Author:  GersonKurz

  ==============================================================================
*/

#include <Audio/ExportMixImplementation.h>
#include <Utils/AssortedUtils.h>

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
            juce::File sourceFile{pathToString(trackInfoPtr->filepath)};
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
                spdlog::error("MTE: Failed to create reader for track ID {} ({})", id, pathToString(trackInfoPtr->filepath));
            }
        }

        ExportMixImplementation::ExportMixImplementation(MixId mixId, const TrackLibrary &trackLibrary, const std::filesystem::path &targetFilepath,
                                                         MixExporterProgressCallback progressCallback)
            : MixProjectLoader{mixId, trackLibrary},
              m_progressCallback{progressCallback},
              m_targetFilepath{targetFilepath},
              m_totalMixDurationMs{Duration_t::zero()}
        {
            if (m_progressCallback)
                m_progressCallback(0.0f, "Starting export...");
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

        bool ExportMixImplementation::calculateMixDuration()
        {
            assert(m_totalMixDurationMs == Duration_t::zero());
            if (m_mixTracks.empty())
            {
                return fail("MTE: No mix tracks found for mix ID " + std::to_string(m_mixId));
            }

            const auto &lastTrackDef = m_mixTracks.back();
            const auto lastTrackInfoOpt = getTrackInfoForId(lastTrackDef.trackId);
            if (!lastTrackInfoOpt)
            {
                return fail("MTE: Last track ID " + std::to_string(lastTrackDef.trackId) + " not found in database.");
            }

            // Effective duration of last track in the mix
            Duration_t lastTrackEffectiveDuration = std::min(lastTrackInfoOpt->duration, lastTrackDef.cutoffTime) - lastTrackDef.silenceStart;

            // TBD: not sure what to do in this case here:
            if (lastTrackEffectiveDuration < Duration_t::zero())
                lastTrackEffectiveDuration = Duration_t::zero();

            m_totalMixDurationMs = lastTrackDef.mixStartTime + lastTrackEffectiveDuration;

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

                // 1. Calculate gain from MixTrack's internal fades (fadeInStart/End, fadeOutStart/End)
                float internalFadeGain; // Will be assigned directly by the equal power logic

                // Pre-calculate these as before
                const juce::int64 fadeInStartSamples = static_cast<juce::int64>((mixTrackDef.fadeInStart.count() / 1000.0) * outputSampleRate());
                const juce::int64 fadeInEndSamples = static_cast<juce::int64>((mixTrackDef.fadeInEnd.count() / 1000.0) * outputSampleRate());
                const juce::int64 fadeOutStartSamples = static_cast<juce::int64>((mixTrackDef.fadeOutStart.count() / 1000.0) * outputSampleRate());
                const juce::int64 fadeOutEndSamples = static_cast<juce::int64>((mixTrackDef.fadeOutEnd.count() / 1000.0) * outputSampleRate());

                float fadeInDuration = (float)(fadeInEndSamples - fadeInStartSamples);
                if (fadeInDuration <= 0)
                    fadeInDuration = 1.0f; // Avoid div by zero, effectively makes it an instant switch if duration is 0 or negative

                float fadeOutDuration = (float)(fadeOutEndSamples - fadeOutStartSamples);
                if (fadeOutDuration <= 0)
                    fadeOutDuration = 1.0f;

                // currentSampleInThisMixTrack is also calculated as before

                // --- Direct Equal Power Fade Logic ---
                if (currentSampleInThisMixTrack < fadeInStartSamples || currentSampleInThisMixTrack >= fadeOutEndSamples)
                {
                    // Before the track's fade-in starts OR at or after its fade-out ends
                    internalFadeGain = 0.0f;
                }
                else if (currentSampleInThisMixTrack < fadeInEndSamples)
                {
                    // During fade-in period (currentSampleInThisMixTrack >= fadeInStartSamples is implied)
                    float progress = (float)(currentSampleInThisMixTrack - fadeInStartSamples) / fadeInDuration;
                    internalFadeGain = std::sin(progress * juce::MathConstants<float>::halfPi); // sin(0) to sin(pi/2) -> 0 to 1
                }
                else if (currentSampleInThisMixTrack >= fadeOutStartSamples)
                {
                    // During fade-out period (currentSampleInThisMixTrack < fadeOutEndSamples is implied by the first if)
                    float progress = (float)(currentSampleInThisMixTrack - fadeOutStartSamples) / fadeOutDuration;
                    internalFadeGain = std::cos(progress * juce::MathConstants<float>::halfPi); // cos(0) to cos(pi/2) -> 1 to 0
                }
                else
                {
                    // Between fadeInEnd and fadeOutStart: full volume
                    internalFadeGain = 1.0f;
                }

                // It's still good practice to clamp, especially if dealing with floating point math that might slightly exceed bounds.
                internalFadeGain = juce::jlimit(0.0f, 1.0f, internalFadeGain);
                // 2. Calculate gain from crossfade with *previous* track (if applicable)
                // This requires knowing the *previous* track in the mixTracks list and its end time.
                // This is the most complex part: a track might be fading IN due to its own MixTrack.fadeIn
                // AND fading IN due to a crossfade from the PREVIOUS track ending.
                // Or it might be fading OUT due to its own MixTrack.fadeOut
                // AND fading OUT because the NEXT track is crossfading in.
                // The current simple `runStitchingTest` approach (gainA = 1-prog, gainB = prog) works for two tracks.
                // For a multi-track mix, each sample in masterOutputBlock is a sum.
                // A simpler model: each track contributes its audio (after internal fades) to the masterOutputBlock.
                // The crossfade is implicitly handled by two tracks being active and summed during their overlap.
                // The `MixTrack.crossfadeDuration` primarily sets `mixStartTime` of the *next* track.
                // The actual fade curves (linear, equal power) happen here. For now, linear.

                // Let's assume the `mixTrackDef.fadeIn/Out` fields are for the *crossfade itself*.
                // If mixTrackDef.fadeInStart/End is 0-5s, it means this track fades in over 5s *as part of the crossfade*.
                // This interpretation makes more sense with your `MixTrack` struct.

                float overallGain = internalFadeGain; // Start with internal fade gain

                // Apply overall volume adjustment for the track
                // Assuming volumeAtStart/End are for a ramp over the track's visible duration
                float trackProgress = (float)currentSampleInThisMixTrack / (float)juce::jmax((juce::int64)1, context.trackFileEffectiveDurationSamples);
                float volumeEnvelope = (1.0f - trackProgress) * (mixTrackDef.volumeAtStart / (float)VOLUME_NORMALIZATION) +
                                       trackProgress * (mixTrackDef.volumeAtEnd / (float)VOLUME_NORMALIZATION);
                overallGain *= volumeEnvelope;

                // Add to master output block
                // The s_idx_in_block maps to where in sourceTrackBlock we read the data,
                // and where in masterOutputBlock we need to write (offset by when this track starts in the block)
                const juce::int64 targetSampleInMasterBlock = currentSampleInOutputTimeline - context.currentBlockStartTimeSamples;
                if (targetSampleInMasterBlock >= 0 && targetSampleInMasterBlock < context.samplesToProcessInThisBlock)
                {
                    if (s_idx_in_block < 5 && context.samplesWrittenTotal < 8192)
                    { // Log first few samples of first few blocks
                        spdlog::debug("MTE DEBUG: s_idx_in_block: {}, overallGain: {}, internalFadeGain: {}, volumeEnvelope: {}", s_idx_in_block, overallGain,
                                      internalFadeGain, volumeEnvelope);
                        // Also log sourceTrackBlock.getSample(0, (int)s_idx_in_block) to see if source data has values
                        if (sourceTrackBlock.getNumChannels() > 0)
                        {
                            spdlog::debug("MTE DEBUG: sourceSample[0]: {}", sourceTrackBlock.getSample(0, (int)s_idx_in_block));
                        }
                    }
                    for (unsigned int chan = 0; chan < outputNumChannels(); ++chan)
                    {
                        masterOutputBlock.addSample(chan, (int)targetSampleInMasterBlock, sourceTrackBlock.getSample(chan, (int)s_idx_in_block) * overallGain);
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
        bool ExportMixImplementation::contributeFromActiveSource(ActiveTrackSource &activeSource,     // Non-const to update reader pos
                                                                 const SampleContext &overallContext, // Overall timeline context
                                                                 juce::AudioBuffer<float> &masterOutputBlock)
        {
            const MixTrack &mixTrackDef = *activeSource.mixTrackDefPtr;
            const TrackInfo &trackInfo = *activeSource.trackInfoPtr;
            juce::AudioFormatReader *reader = activeSource.reader.get(); // Get raw pointer
            // juce::AudioSource* actualSourceToReadFrom = activeSource.resamplerSource ? activeSource.resamplerSource.get() :
            // activeSource.readerSource.get();

            // --- Calculate timing for *this specific track* based on mixTrackDef ---
            juce::int64 trackMixStartSamples = static_cast<juce::int64>((mixTrackDef.mixStartTime.count() / 1000.0) * outputSampleRate());
            Duration_t trackFileEffectiveDurationMs = std::min(trackInfo.duration, mixTrackDef.cutoffTime) - mixTrackDef.silenceStart;
            if (trackFileEffectiveDurationMs < Duration_t::zero())
                trackFileEffectiveDurationMs = Duration_t::zero();
            const juce::int64 trackFileEffectiveDurationSamples =
                static_cast<juce::int64>((trackFileEffectiveDurationMs.count() / 1000.0) * outputSampleRate());
            juce::int64 trackMixEndSamples = trackMixStartSamples + trackFileEffectiveDurationSamples;

            // --- Check if this track is active in the current masterOutputBlock ---
            if (trackMixEndSamples <= overallContext.currentBlockStartTimeSamples || trackMixStartSamples >= overallContext.currentBlockEndTimeSamples)
            {
                // spdlog::warn("Track ID {} is not active in current block ({} - {}). Skipping.", mixTrackDef.trackId,
                //              overallContext.currentBlockStartTimeSamples, overallContext.currentBlockEndTimeSamples);
                return true; // Not active in this block
            }

            // --- Track is active: Use its pre-opened reader ---
            juce::AudioBuffer<float> sourceTrackBlock(
                outputNumChannels(),
                (int)overallContext.samplesToProcessInThisBlock); // Buffer for this track's contribution to the current output block
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

            juce::int64 readOffsetInSourceFileSamples =
                (readStartInOutputTimeline - trackMixStartSamples) + static_cast<juce::int64>((mixTrackDef.silenceStart.count() / 1000.0) * reader->sampleRate);

            // How many samples to read from source into our local sourceTrackBlock.
            // The reader reads from its current position.
            // We need to seek the reader or manage its position carefully if not using AudioSource interface.
            // Using reader->read() directly:
            const auto readSuccess = reader->read(&sourceTrackBlock, 0, (int)numSamplesToReadForThisTrackInBlock, readOffsetInSourceFileSamples, true, true);
            if (!readSuccess)
            {
                spdlog::error("MTE: Failed to read samples for track ID {} from source file: {}", mixTrackDef.trackId, pathToString(trackInfo.filepath));
                return false; // Error reading
            }
            // If using readerSource.getNextAudioBlock(), it manages its own position, but you need to feed it blocks.
            // The current direct reader->read() with an absolute offset is simpler if not using full AudioSource chain.

            // SampleContext for this specific track's contribution within the block
            SampleContext trackContext = overallContext; // Copy relevant parts
            trackContext.numSamplesToReadFromSource = numSamplesToReadForThisTrackInBlock;
            trackContext.readStartInOutputTimeline = readStartInOutputTimeline; // When this track's contribution starts in the output timeline
            trackContext.trackMixStartSamples = trackMixStartSamples;           // Start of this track in overall mix
            trackContext.trackFileEffectiveDurationSamples = trackFileEffectiveDurationSamples;

            // This function now takes the already-read sourceTrackBlock data
            return applyMixTrackSpecs(mixTrackDef, trackContext, masterOutputBlock, sourceTrackBlock);
        }

    } // namespace audio
} // namespace jucyaudio
