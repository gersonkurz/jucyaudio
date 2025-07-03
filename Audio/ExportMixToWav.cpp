/*
  ==============================================================================

    MixExporter.cpp
    Created: 1 Jun 2025 8:08:15pm
    Author:  GersonKurz

  ==============================================================================
*/

#include <Audio/ExportMixToWav.h>

namespace jucyaudio
{
    namespace audio
    {
        using namespace jucyaudio::database;

        ExportMixImplementation::ExportMixImplementation(MixId mixId, const TrackLibrary &trackLibrary, const std::filesystem::path &targetFilepath,
                                                         MixExporterProgressCallback progressCallback)
            : m_trackLibrary{trackLibrary},
              m_mixTracks{trackLibrary.getMixManager().getMixTracks(mixId)},
              m_trackInfos{trackLibrary.getTracks(getMixTrackQueryArgs(mixId))},
              m_mixId{mixId},
              m_progressCallback{progressCallback},
              m_targetFilepath{targetFilepath},
              m_trackInfosMap{},
              m_totalMixDurationMs{Duration_t::zero()}
        {
            for (const auto &ti : m_trackInfos)
            {
                m_trackInfosMap[ti.trackId] = &ti; // Cache track infos by ID for quick access
            }
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
            const auto lastTrackInfoOpt = getTrackInfoById(lastTrackDef.trackId);
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
                if (const auto trackInfo{getTrackInfoById(mixTrackDef.trackId)})
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

        bool ExportWavMixImplementation::onSetupAudioFormatManagerAndWriter()
        {
            m_formatManager.registerBasicFormats(); // For reading various input formats
                                                    // For MP3 writing, LAME will be handled separately or via a Juce LAME format.

            // targetFilepath.parent_path().create_directories();                 // Ensure output directory exists
            juce::File outputFile{pathToString(m_targetFilepath)};
            if (outputFile.existsAsFile())
            {
                outputFile.deleteFile();
            }
            std::unique_ptr<juce::FileOutputStream> fileOutputStream(outputFile.createOutputStream());
            if (!fileOutputStream)
            {
                return fail("MTE: Could not create output file stream for " + pathToString(m_targetFilepath));
            }

            juce::WavAudioFormat wavFormat;
            m_writer.reset(wavFormat.createWriterFor(fileOutputStream.get(), outputSampleRate(), outputNumChannels(), outputBitDepth(), {}, 0));
            if (!m_writer)
            {
                spdlog::info("MTE: unable to create WavAudioFormat writer for file: {}", pathToString(m_targetFilepath));
                return false;
            }
            fileOutputStream.release(); // Writer now owns the stream
            return true;
        }

        bool ExportWavMixImplementation::onRunMixingLoop()
        {
            // Create a master output buffer for a block of samples
            const int processingBlockSize = 4096; // samples
            juce::AudioBuffer<float> masterOutputBlock{(int)outputNumChannels(), processingBlockSize};

            SampleContext context;

            //const auto overallStart = clock::now();
            // Iterate through the output mix timeline, block by block
            while (context.samplesWrittenTotal < m_totalOutputSamples)
            {
                masterOutputBlock.clear();
                context.currentBlockStartTimeSamples = context.samplesWrittenTotal;
                context.currentBlockEndTimeSamples = context.samplesWrittenTotal + processingBlockSize;
                context.samplesToProcessInThisBlock = juce::jmin((juce::int64)processingBlockSize, m_totalOutputSamples - context.samplesWrittenTotal);
                if (context.samplesToProcessInThisBlock <= 0)
                    break;

                // For each block in the output timeline:
                // Iterate through ALL MixTrack definitions

                for (auto &activeSource : m_activeSources)
                { // Note: non-const to modify reader state (position)
                    if (!activeSource.reader)
                        continue; // Skip if reader failed to init

                    // No per-block timing needed for the log here, use overall block timing.
                    contributeFromActiveSource(activeSource, context, masterOutputBlock);
                }

                // Write the processed masterOutputBlock to the file
                m_writer->writeFromAudioSampleBuffer(masterOutputBlock, 0, (int)context.samplesToProcessInThisBlock);
                context.samplesWrittenTotal += context.samplesToProcessInThisBlock;

                if (m_progressCallback)
                {
                    float progress = (float)context.samplesWrittenTotal / (float)m_totalOutputSamples;
                    m_progressCallback(progress, "Exporting...");
                }
                // const auto overallEnd = clock::now();
                // const auto overallDuration = std::chrono::duration_cast<std::chrono::milliseconds>(overallEnd - overallStart).count();
                //  spdlog::debug("MTE: overall elapsed {} ms ({})", overallDuration, durationToString((Duration_t)overallDuration));

            } // end while samplesWrittenTotal < m_totalOutputSamples

            m_writer->flush();
            // Writer (and its owned stream) and readers are cleaned up by unique_ptr.
            spdlog::info("Mix export finished for mix ID: {}", m_mixId);
            if (m_progressCallback)
                m_progressCallback(1.0f, "Export complete.");
            return true;
        }

        ExportMp3MixImplementation::~ExportMp3MixImplementation()
        {
            if (m_lameFlags)
            {
                lame_close(m_lameFlags);
                m_lameFlags = nullptr;
            }
            delete[] m_mp3Buffer;
            m_mp3Buffer = nullptr;
            // m_outputStream automatically cleaned up by unique_ptr
        }

        bool ExportMp3MixImplementation::onSetupAudioFormatManagerAndWriter()
        {
            m_formatManager.registerBasicFormats(); // For reading input formats

            // Create output file stream
            juce::File outputFile{pathToString(m_targetFilepath)};
            if (outputFile.existsAsFile())
            {
                outputFile.deleteFile();
            }

            m_outputStream = std::unique_ptr<juce::FileOutputStream>(outputFile.createOutputStream());
            if (!m_outputStream)
            {
                return fail("MTE: Could not create output file stream for " + pathToString(m_targetFilepath));
            }

            // Initialize LAME
            m_lameFlags = lame_init();
            if (!m_lameFlags)
            {
                return fail("MTE: lame_init() failed");
            }

            // Configure LAME settings
            lame_set_in_samplerate(m_lameFlags, static_cast<int>(outputSampleRate()));
            lame_set_num_channels(m_lameFlags, static_cast<int>(outputNumChannels()));
            // *** CBR 320 kbps ***
            lame_set_VBR(m_lameFlags, vbr_off); // turn VBR OFF
            lame_set_brate(m_lameFlags, 320);   // target bitrate, kbit/s
            lame_set_quality(m_lameFlags, 2);   // psy-model quality (0=best, 9=worst)

            // (optional but recommended)
            lame_set_mode(m_lameFlags, JOINT_STEREO); // joint stereo saves a few bits
                                                      // Add basic ID3 tags
            id3tag_init(m_lameFlags);
            id3tag_set_artist(m_lameFlags, "jucyaudio");
            id3tag_set_album(m_lameFlags, "jucyaudio Mixes");
            id3tag_set_year(m_lameFlags, "2025");

            // Initialize LAME parameters
            int lame_ret = lame_init_params(m_lameFlags);
            if (lame_ret < 0)
            {
                return fail(std::format("MTE: lame_init_params() failed with code: {}", lame_ret));
            }
            unsigned char id3v2[10 * 1024];
            size_t id3bytes = lame_get_id3v2_tag(m_lameFlags, id3v2, sizeof id3v2);
            m_outputStream->write(id3v2, id3bytes);
            spdlog::debug("LAME initialized: SR={}, Channels={}, Mode={}", lame_get_in_samplerate(m_lameFlags), lame_get_num_channels(m_lameFlags),
                          (int)lame_get_mode(m_lameFlags));
            // Allocate MP3 buffer
            const int processingBlockSize = 4096;
            m_mp3BufferSize = static_cast<int>(1.25 * processingBlockSize) + 7200;
            m_mp3Buffer = new unsigned char[m_mp3BufferSize];
            if (!m_mp3Buffer)
            {
                return fail("MTE: Failed to allocate MP3 buffer");
            }

            spdlog::info("MTE: LAME encoder initialized for MP3 output. Buffer size: {}", m_mp3BufferSize);
            return true;
        }

        bool ExportMp3MixImplementation::onRunMixingLoop()
        {
            // Create a master output buffer for a block of samples
            const int processingBlockSize = 4096; // samples
            juce::AudioBuffer<float> masterOutputBlock{(int)outputNumChannels(), processingBlockSize};

            SampleContext context;

            // Iterate through the output mix timeline, block by block
            while (context.samplesWrittenTotal < m_totalOutputSamples)
            {
                masterOutputBlock.clear();
                context.currentBlockStartTimeSamples = context.samplesWrittenTotal;
                context.currentBlockEndTimeSamples = context.samplesWrittenTotal + processingBlockSize;
                context.samplesToProcessInThisBlock = juce::jmin((juce::int64)processingBlockSize, m_totalOutputSamples - context.samplesWrittenTotal);
                if (context.samplesToProcessInThisBlock <= 0)
                    break;

                // Fill the master output block (same logic as WAV)
                for (auto &activeSource : m_activeSources)
                {
                    if (!activeSource.reader)
                        continue;
                    contributeFromActiveSource(activeSource, context, masterOutputBlock);
                }

                // --- MP3 Encoding with LAME (NOT using m_writer) ---
                if (!m_lameFlags || !m_mp3Buffer || !m_outputStream)
                {
                    return fail("MTE: LAME encoder not properly initialized for MP3 export");
                }

                const int numPcmSamplesForLame = static_cast<int>(context.samplesToProcessInThisBlock);
                const float *leftChannelData = masterOutputBlock.getReadPointer(0);
                const float *rightChannelData = (outputNumChannels() > 1) ? masterOutputBlock.getReadPointer(1) : leftChannelData;

                // Debug: Check if we have real audio data
                if (context.samplesWrittenTotal < 81920)
                { // First couple blocks only
                    spdlog::debug("MP3 DEBUG: Block samples={}, left[0]={}, left[100]={}, right[0]={}, right[100]={}", numPcmSamplesForLame, leftChannelData[0],
                                  leftChannelData[juce::jmin(100, numPcmSamplesForLame - 1)], rightChannelData[0],
                                  rightChannelData[juce::jmin(100, numPcmSamplesForLame - 1)]);
                }
                // Encode with LAME
                // int bytes_encoded =
                //    lame_encode_buffer_float(m_lameFlags, leftChannelData, rightChannelData, numPcmSamplesForLame, m_mp3Buffer, m_mp3BufferSize);
                const int n = static_cast<int>(context.samplesToProcessInThisBlock);

                std::vector<float> interleaved(static_cast<size_t>(n * 2));
                const float *L = masterOutputBlock.getReadPointer(0);
                const float *R = masterOutputBlock.getReadPointer(1);
                for (int i = 0; i < n; ++i)
                {
                    interleaved[2 * i] = L[i];
                    interleaved[2 * i + 1] = R[i];
                }
                int bytes_encoded = lame_encode_buffer_interleaved_ieee_float(m_lameFlags,
                                                                              interleaved.data(), // the array we just built
                                                                              n,                  // samples *per channel*
                                                                              m_mp3Buffer, m_mp3BufferSize);
                if (bytes_encoded < 0)
                {
                    return fail(std::format("MTE: lame_encode_buffer_float() failed with error: {}", bytes_encoded));
                }

                if (bytes_encoded > 0)
                {
                    spdlog::debug("LAME encode: input_samples={}, bytes_out={}, first_bytes=[{:02x} {:02x} {:02x} {:02x}]", numPcmSamplesForLame, bytes_encoded,
                                  bytes_encoded > 0 ? m_mp3Buffer[0] : 0, bytes_encoded > 1 ? m_mp3Buffer[1] : 0, bytes_encoded > 2 ? m_mp3Buffer[2] : 0,
                                  bytes_encoded > 3 ? m_mp3Buffer[3] : 0);

                    if (!m_outputStream->write(m_mp3Buffer, static_cast<size_t>(bytes_encoded)))
                    {
                        return fail("MTE: Failed to write encoded MP3 data to output stream");
                    }
                }

                context.samplesWrittenTotal += context.samplesToProcessInThisBlock;

                if (m_progressCallback)
                {
                    float progress = (float)context.samplesWrittenTotal / (float)m_totalOutputSamples;
                    m_progressCallback(progress, "Exporting...");
                }
            }

            // Flush remaining MP3 data
            int flush_bytes = lame_encode_flush(m_lameFlags, m_mp3Buffer, m_mp3BufferSize);
            if (flush_bytes < 0)
            {
                return fail(std::format("MTE: lame_encode_flush() failed with error: {}", flush_bytes));
            }

            if (flush_bytes > 0)
            {
                if (!m_outputStream->write(m_mp3Buffer, static_cast<size_t>(flush_bytes)))
                {
                    return fail("MTE: Failed to write flushed MP3 data to output stream");
                }
            }

            m_outputStream->flush();
            unsigned char info[8100];
            size_t infoBytes = lame_get_lametag_frame(m_lameFlags, info, sizeof info);
            m_outputStream->write(info, infoBytes);

            // (optional) ID3v1 footer:
            unsigned char id3v1[128];
            size_t id3v1bytes = lame_get_id3v1_tag(m_lameFlags, id3v1, sizeof id3v1);
            m_outputStream->write(id3v1, id3v1bytes);

            spdlog::info("MP3 export finished for mix ID: {}", m_mixId);
            if (m_progressCallback)
                m_progressCallback(1.0f, "Export complete.");

            return true;
        }
    } // namespace audio
} // namespace jucyaudio
