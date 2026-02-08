#include <Database/BackgroundTasks/AudioAnalysis.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>
#include <BPMDetect.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <spdlog/spdlog.h>
#include <memory>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        namespace background_tasks
        {
            namespace
            {
                float detectBPM(const juce::AudioBuffer<float> &buffer, double sampleRate)
                {
                    // SoundTouch BPMDetect works with mono audio
                    const int numChannels = 1;
                    soundtouch::BPMDetect bpmDetector(numChannels, static_cast<int>(sampleRate));

                    // Convert to mono if needed and feed to BPMDetect
                    if (buffer.getNumChannels() > 1)
                    {
                        // Mix down to mono
                        std::vector<float> monoSamples(buffer.getNumSamples());
                        for (int i = 0; i < buffer.getNumSamples(); ++i)
                        {
                            float sample = 0.0f;
                            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                            {
                                sample += buffer.getReadPointer(ch)[i];
                            }
                            monoSamples[i] = sample / buffer.getNumChannels();
                        }
                        bpmDetector.inputSamples(monoSamples.data(), buffer.getNumSamples());
                    }
                    else
                    {
                        bpmDetector.inputSamples(buffer.getReadPointer(0), buffer.getNumSamples());
                    }

                    // Get BPM result
                    float bpm = bpmDetector.getBpm();

                    // SoundTouch returns 0 if it couldn't detect BPM
                    // Filter to reasonable range (60-200 BPM for most music)
                    if (bpm < 60.0f || bpm > 200.0f)
                    {
                        return 0.0f;
                    }

                    return bpm;
                }
            } // anonymous namespace

            AudioMetadata analyzeAudioBuffer(const juce::AudioBuffer<float>& buffer, double sampleRate)
            {
                AudioMetadata metadata;

                if (buffer.getNumSamples() == 0)
                    return metadata;

                metadata.bpm = detectBPM(buffer, sampleRate);
                return metadata;
            }

            AudioMetadata analyzeAudioFile(const std::filesystem::path &filepath)
            {
                AudioMetadata metadata;

                // Initialize JUCE audio format manager
                juce::AudioFormatManager formatManager;
                formatManager.registerBasicFormats();

                // Load the audio file
                juce::File audioFile{ui::jucePathFromFs(filepath)};
                if (!audioFile.existsAsFile())
                {
                    spdlog::error("Audio file does not exist: {}", pathToString(filepath));
                    return metadata;
                }

                std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(audioFile));
                if (!reader)
                {
                    spdlog::error("Could not create audio format reader for: {}", pathToString(filepath));
                    return metadata;
                }

                // For BPM detection, analyzing the middle 60 seconds is sufficient and much faster
                const double totalDurationSeconds = reader->lengthInSamples / reader->sampleRate;
                const double analysisDurationSeconds = 60.0;
                int64_t startSample = 0;
                int numSamplesToRead = static_cast<int>(reader->lengthInSamples);

                if (totalDurationSeconds > analysisDurationSeconds)
                {
                    // Start reading from the middle of the track
                    startSample = static_cast<int64_t>(((totalDurationSeconds / 2.0) - (analysisDurationSeconds / 2.0)) * reader->sampleRate);
                    numSamplesToRead = static_cast<int>(analysisDurationSeconds * reader->sampleRate);

                    // Ensure we don't read past the end of the file
                    if (startSample + numSamplesToRead > reader->lengthInSamples)
                    {
                        numSamplesToRead = static_cast<int>(reader->lengthInSamples - startSample);
                    }
                }

                // Read the selected audio portion into a buffer
                const int numChannels = static_cast<int>(reader->numChannels);
                const double sampleRate = reader->sampleRate;

                juce::AudioBuffer<float> audioBuffer(numChannels, numSamplesToRead);
                reader->read(&audioBuffer, 0, numSamplesToRead, startSample, true, true);

                // Perform BPM analysis
                metadata = analyzeAudioBuffer(audioBuffer, sampleRate);

                return metadata;
            }
        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio
