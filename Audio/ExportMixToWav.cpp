/*
  ==============================================================================

    MixExporter.cpp
    Created: 1 Jun 2025 8:08:15pm
    Author:  GersonKurz

  ==============================================================================
*/

#include <Audio/ExportMixToWav.h>
#include <Audio/Plugins/PluginChain.h>
#include <format>

namespace jucyaudio
{
    namespace audio
    {
        using namespace database;
        bool ExportWavMixImplementation::onSetupAudioFormatManagerAndWriter()
        {
            m_formatManager.registerBasicFormats(); // For reading various input formats
                                                    // For MP3 writing, LAME will be handled separately or via a Juce LAME format.

            // The partial file, not the target: the two steps that can still fail come after this one,
            // and this used to delete the previous export before either of them ran. See
            // ExportMixImplementation::renderTargetPath.
            const auto exportPath{pathToString(renderTargetPath())};

            juce::File outputFile{exportPath};
            if (outputFile.existsAsFile())
            {
                // Left over from an export that was interrupted rather than one that finished.
                outputFile.deleteFile();
            }
            std::unique_ptr<juce::FileOutputStream> fileOutputStream(outputFile.createOutputStream());
            if (!fileOutputStream)
            {
                return fail("MTE: Could not create output file stream for " + exportPath);
            }

            juce::WavAudioFormat wavFormat;

            auto options = juce::AudioFormatWriterOptions{}
                .withSampleRate(outputSampleRate())
                .withNumChannels(static_cast<int>(outputNumChannels()))
                .withBitsPerSample(static_cast<int>(outputBitDepth()))
                .withQualityOptionIndex(0);

            std::unique_ptr<juce::OutputStream> streamPtr(fileOutputStream.release());
            m_writer = wavFormat.createWriterFor(streamPtr, options);
            if (!m_writer)
            {
                spdlog::info("MTE: unable to create WavAudioFormat writer for file: {}", exportPath);
                return false;
            }
            // Writer now owns the stream via streamPtr
            return true;
        }

        bool ExportWavMixImplementation::releaseOutput()
        {
            // The reset is what patches the header, so the file is only worth inspecting afterwards.
            ExportMixImplementation::releaseOutput();

            // Read back rather than measured. Measuring the file only proves the samples are there,
            // and the failure this is about leaves them there: if the seek-and-rewrite of the RIFF
            // header does not land, the payload is intact and complete while the header still
            // advertises the length it was created with. A reader parses that header, so asking one
            // how long the file is asks the question that matters. There is nothing left to ask the
            // stream - the writer owned it and took it with it.
            // WavAudioFormat directly, not through the format manager: the manager chooses a format by
            // file extension, and the partial's is ".jucyaudio-part", which nothing claims. This class
            // wrote the file, so it already knows what format to read it back as.
            juce::WavAudioFormat wavFormat;
            const juce::File rendered{pathToString(renderTargetPath())};
            std::unique_ptr<juce::AudioFormatReader> reader{wavFormat.createReaderFor(new juce::FileInputStream{rendered}, true)};
            if (!reader)
            {
                spdlog::error("MTE: {} could not be read back after writing it.", pathToString(renderTargetPath()));
                return false;
            }

            if (reader->lengthInSamples != m_totalOutputSamples)
            {
                spdlog::error("MTE: {} reads back as {} samples, not the {} the mix rendered; its header did not survive the close.",
                    pathToString(renderTargetPath()),
                    reader->lengthInSamples,
                    m_totalOutputSamples);
                return false;
            }

            return true;
        }

        bool ExportWavMixImplementation::onRunMixingLoop()
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

            //const auto overallStart = clock::now();
            // Track unique failed tracks using base class member for structured result
            m_failedTracks.clear();

            // Iterate through the output mix timeline, block by block
            const auto previousPrep = audio::theMasterPluginChain.getPreparationState();
            audio::theMasterPluginChain.prepareToPlay(outputSampleRate(), processingBlockSize);
            struct ChainRestoreGuard
            {
                audio::PluginChain::PreparationState previous;
                ~ChainRestoreGuard()
                {
                    if (previous.prepared)
                    {
                        audio::theMasterPluginChain.prepareToPlay(previous.sampleRate, previous.blockSize);
                    }
                    else
                    {
                        audio::theMasterPluginChain.releaseResources();
                    }
                }
            };
            ChainRestoreGuard restoreGuard{previousPrep};

            while (context.samplesWrittenTotal < m_totalOutputSamples)
            {
                masterOutputBlock.clear();
                context.currentBlockStartTimeSamples = context.samplesWrittenTotal;
                context.currentBlockEndTimeSamples = context.samplesWrittenTotal + processingBlockSize;
                context.samplesToProcessInThisBlock = juce::jmin((juce::int64)processingBlockSize, m_totalOutputSamples - context.samplesWrittenTotal);
                if (context.samplesToProcessInThisBlock <= 0)
                    break;

                // For each block in the output timeline, iterate through all active sources by index
                for (size_t i = 0; i < m_activeSources.size(); ++i)
                {
                    if (!contributeFromActiveSource(i, context, masterOutputBlock))
                    {
                        m_failedTracks.insert(m_activeSources[i].trackId);
                    }
                }

                juce::AudioBuffer<float> pluginBuffer{
                    masterOutputBlock.getArrayOfWritePointers(),
                    masterOutputBlock.getNumChannels(),
                    0,
                    static_cast<int>(context.samplesToProcessInThisBlock)};
                audio::theMasterPluginChain.processBlock(pluginBuffer);

                // Write the processed masterOutputBlock to the file.
                //
                // Checked, because the answer decides whether the previous export survives: run()
                // commits the rendered file over the target when this loop reports success, so a write
                // that failed and was ignored - a full disk is the ordinary way - replaced a complete
                // export with a truncated one.
                if (!m_writer->writeFromAudioSampleBuffer(masterOutputBlock, 0, (int)context.samplesToProcessInThisBlock))
                {
                    return fail(std::format("MTE: could not write {} samples at {} to {}",
                        context.samplesToProcessInThisBlock,
                        context.samplesWrittenTotal,
                        pathToString(renderTargetPath())));
                }
                context.samplesWrittenTotal += context.samplesToProcessInThisBlock;

                if (m_progressCallback)
                {
                    float progress = (float)context.samplesWrittenTotal / (float)m_totalOutputSamples;
                    currentTrackNumber = getCurrentTrackNumber();
                    const auto progressMessage = std::format("Exporting WAV... Track {}/{} ({:.0f}%)",
                        currentTrackNumber, totalTracks, progress * 100.0f);
                    m_progressCallback(progress, progressMessage);
                }
            } // end while samplesWrittenTotal < m_totalOutputSamples

            // Also checked: this is where a buffered write finally reaches the disk, so it is the last
            // chance to find out that the file is not what it should be before run() commits it.
            if (!m_writer->flush())
            {
                return fail(std::format("MTE: could not flush {}", pathToString(renderTargetPath())));
            }
            // Writer (and its owned stream) and readers are cleaned up by unique_ptr.

            if (!m_failedTracks.empty())
            {
                spdlog::warn("WAV export completed with {} failed track(s) for mix ID: {} - some audio may be missing", m_failedTracks.size(), m_mixId);
            }
            else
            {
                spdlog::info("WAV export finished for mix ID: {}", m_mixId);
            }

            if (m_progressCallback)
                m_progressCallback(1.0f, !m_failedTracks.empty() ? "WAV export complete (with warnings)" : "WAV export complete!");
            return true;
        }
    } // namespace audio
} // namespace jucyaudio
