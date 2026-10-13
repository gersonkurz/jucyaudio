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
        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio
