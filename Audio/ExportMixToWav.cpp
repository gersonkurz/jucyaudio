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
        using namespace database;

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

    } // namespace audio
} // namespace jucyaudio
