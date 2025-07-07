/*
  ==============================================================================

    MixExporter.cpp
    Created: 1 Jun 2025 8:08:15pm
    Author:  GersonKurz

  ==============================================================================
*/

#include <Audio/ExportMixToMp3.h>

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
