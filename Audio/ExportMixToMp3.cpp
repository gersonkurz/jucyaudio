/*
  ==============================================================================

    MixExporter.cpp
    Created: 1 Jun 2025 8:08:15pm
    Author:  GersonKurz

  ==============================================================================
*/

#include <Audio/ExportMixToMp3.h>
#include <UI/Settings.h>
#include <format>

namespace jucyaudio
{
    namespace audio
    {
        using namespace database;

        ExportMp3MixImplementation::~ExportMp3MixImplementation()
        {
            if (m_lameFlags)
            {
                lame_close(m_lameFlags);
                m_lameFlags = nullptr;
            }
            // m_mp3Buffer and m_outputStream automatically cleaned up
        }

        bool ExportMp3MixImplementation::onSetupAudioFormatManagerAndWriter()
        {
            m_formatManager.registerBasicFormats(); // For reading input formats

            // Create output file stream
            juce::File outputFile{pathToString(m_settings.outputPath)};
            if (outputFile.existsAsFile())
            {
                outputFile.deleteFile();
            }

            m_outputStream = std::unique_ptr<juce::FileOutputStream>(outputFile.createOutputStream());
            if (!m_outputStream)
            {
                return fail("MTE: Could not create output file stream for " + pathToString(m_settings.outputPath));
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
            
            // Add ID3 tags
            id3tag_init(m_lameFlags);

            id3tag_set_artist(m_lameFlags, m_settings.artist.c_str());
            id3tag_set_album(m_lameFlags, m_settings.album.c_str());
            id3tag_set_title(m_lameFlags, m_settings.title.c_str());
            id3tag_set_track(m_lameFlags, m_settings.trackNumber.c_str());
            id3tag_set_year(m_lameFlags, m_settings.year.c_str());
            id3tag_set_genre(m_lameFlags, m_settings.genre.c_str());
            id3tag_set_comment(m_lameFlags, m_settings.comment.c_str());

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
            const auto mp3BufferSize = static_cast<size_t>(1.25 * processingBlockSize) + 7200;
            m_mp3Buffer.resize(mp3BufferSize);

            spdlog::info("MTE: LAME encoder initialized for MP3 output. Buffer size: {}", m_mp3Buffer.size());
            return true;
        }

        bool ExportMp3MixImplementation::onRunMixingLoop()
        {
            // Create a master output buffer for a block of samples
            const int processingBlockSize = 4096; // samples
            juce::AudioBuffer<float> masterOutputBlock{(int)outputNumChannels(), processingBlockSize};

            SampleContext context;

            // Calculate total tracks for progress reporting
            const int totalTracks = static_cast<int>(m_mixTracks.size());
            int currentTrackNumber = 0;
            
            // Track which tracks are currently being processed
            auto getCurrentTrackNumber = [&]() -> int {
                if (m_trackPositions.empty()) return 0;
                
                // Find the primary track at current position
                const auto currentTimeMs = (context.samplesWrittenTotal * 1000) / static_cast<juce::int64>(outputSampleRate());
                for (size_t i = 0; i < m_trackPositions.size(); ++i) {
                    const auto& pos = m_trackPositions[i];
                    if (currentTimeMs >= pos.startTime.count() && currentTimeMs <= pos.endTime.count()) {
                        return static_cast<int>(i + 1);
                    }
                }
                return totalTracks;
            };
            
            // Track unique failed tracks using base class member for structured result
            m_failedTracks.clear();

            // Pre-allocate interleaved buffer outside the loop to avoid per-block heap allocation
            std::vector<float> interleaved(static_cast<size_t>(processingBlockSize * 2));

            // Iterate through the output mix timeline, block by block
            while (context.samplesWrittenTotal < m_totalOutputSamples)
            {
                masterOutputBlock.clear();
                context.currentBlockStartTimeSamples = context.samplesWrittenTotal;
                context.currentBlockEndTimeSamples = context.samplesWrittenTotal + processingBlockSize;
                context.samplesToProcessInThisBlock = juce::jmin((juce::int64)processingBlockSize, m_totalOutputSamples - context.samplesWrittenTotal);
                if (context.samplesToProcessInThisBlock <= 0)
                    break;

                // Fill the master output block by iterating through each track by its index
                for (size_t i = 0; i < m_activeSources.size(); ++i)
                {
                    if (!contributeFromActiveSource(i, context, masterOutputBlock))
                    {
                        m_failedTracks.insert(m_activeSources[i].trackId);
                    }
                }

                // --- MP3 Encoding with LAME (NOT using m_writer) ---
                if (!m_lameFlags || m_mp3Buffer.empty() || !m_outputStream)
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
                const int n = static_cast<int>(context.samplesToProcessInThisBlock);
                const float *L = masterOutputBlock.getReadPointer(0);
                const float *R = masterOutputBlock.getReadPointer(1);
                for (int i = 0; i < n; ++i)
                {
                    interleaved[2 * i] = L[i];
                    interleaved[2 * i + 1] = R[i];
                }
                int bytes_encoded = lame_encode_buffer_interleaved_ieee_float(m_lameFlags,
                                                                              interleaved.data(),
                                                                              n,
                                                                              m_mp3Buffer.data(), static_cast<int>(m_mp3Buffer.size()));
                if (bytes_encoded < 0)
                {
                    return fail(std::format("MTE: lame_encode_buffer_float() failed with error: {}", bytes_encoded));
                }

                if (bytes_encoded > 0)
                {
                    spdlog::debug("LAME encode: input_samples={}, bytes_out={}, first_bytes=[{:02x} {:02x} {:02x} {:02x}]", numPcmSamplesForLame, bytes_encoded,
                                  m_mp3Buffer[0], bytes_encoded > 1 ? m_mp3Buffer[1] : 0, bytes_encoded > 2 ? m_mp3Buffer[2] : 0,
                                  bytes_encoded > 3 ? m_mp3Buffer[3] : 0);

                    if (!m_outputStream->write(m_mp3Buffer.data(), static_cast<size_t>(bytes_encoded)))
                    {
                        return fail("MTE: Failed to write encoded MP3 data to output stream");
                    }
                }

                context.samplesWrittenTotal += context.samplesToProcessInThisBlock;

                if (m_progressCallback)
                {
                    float progress = (float)context.samplesWrittenTotal / (float)m_totalOutputSamples;
                    currentTrackNumber = getCurrentTrackNumber();
                    const auto progressMessage = std::format("Exporting MP3... Track {}/{} ({:.0f}%)", 
                        currentTrackNumber, totalTracks, progress * 100.0f);
                    m_progressCallback(progress, progressMessage);
                }
            }

            // Flush remaining MP3 data
            int flush_bytes = lame_encode_flush(m_lameFlags, m_mp3Buffer.data(), static_cast<int>(m_mp3Buffer.size()));
            if (flush_bytes < 0)
            {
                return fail(std::format("MTE: lame_encode_flush() failed with error: {}", flush_bytes));
            }

            if (flush_bytes > 0)
            {
                if (!m_outputStream->write(m_mp3Buffer.data(), static_cast<size_t>(flush_bytes)))
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

            if (!m_failedTracks.empty())
            {
                spdlog::warn("MP3 export completed with {} failed track(s) for mix ID: {} - some audio may be missing", m_failedTracks.size(), m_mixId);
            }
            else
            {
                spdlog::info("MP3 export finished for mix ID: {}", m_mixId);
            }

            if (m_progressCallback)
                m_progressCallback(1.0f, !m_failedTracks.empty() ? "MP3 export complete (with warnings)" : "MP3 export complete!");

            return true;
        }
    } // namespace audio
} // namespace jucyaudio