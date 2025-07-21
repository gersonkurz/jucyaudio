#include <Database/BackgroundTasks/AudioAnalysis.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>
#include <aubio/aubio.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        namespace background_tasks
        {
            // A private helper class to encapsulate the core analysis logic.
            class AudioAnalyzer
            {
            private:
                static constexpr int WINDOW_SIZE = 1024;
                static constexpr int HOP_SIZE = 512;
                static constexpr double MIN_INTRO_LENGTH = 8.0; // Minimum intro length in seconds
                static constexpr double MIN_OUTRO_LENGTH = 8.0; // Minimum outro length in seconds

                struct EnergyFrame
                {
                    double timestamp;
                    float energy;
                    float spectralCentroid;
                    float spectralRolloff;
                };

                static std::vector<EnergyFrame> calculateEnergyFrames(const juce::AudioBuffer<float> &buffer, double sampleRate)
                {
                    std::vector<EnergyFrame> frames;
                    const int numSamples = buffer.getNumSamples();
                    const int frameSize = static_cast<int>(sampleRate * 0.1); // 100ms frames
                    const int hopSize = frameSize / 2;

                    for (int i = 0; i < numSamples - frameSize; i += hopSize)
                    {
                        EnergyFrame frame;
                        frame.timestamp = static_cast<double>(i) / sampleRate;

                        // Calculate RMS energy
                        float rms = 0.0f;
                        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        {
                            const float *channelData = buffer.getReadPointer(ch);
                            for (int j = 0; j < frameSize; ++j)
                            {
                                rms += channelData[i + j] * channelData[i + j];
                            }
                        }
                        frame.energy = std::sqrt(rms / (frameSize * buffer.getNumChannels()));

                        // Calculate spectral centroid (simplified)
                        frame.spectralCentroid = calculateSpectralCentroid(buffer, i, frameSize);

                        // Calculate spectral rolloff (simplified)
                        frame.spectralRolloff = calculateSpectralRolloff(buffer, i, frameSize);

                        frames.push_back(frame);
                    }

                    return frames;
                }

                static float calculateSpectralCentroid(const juce::AudioBuffer<float> &buffer, int startSample, int frameSize)
                {
                    // Simplified spectral centroid calculation
                    // In a real implementation, you'd use FFT here
                    float weightedSum = 0.0f;
                    float magnitudeSum = 0.0f;

                    for (int i = 0; i < frameSize; ++i)
                    {
                        float sample = 0.0f;
                        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        {
                            sample += std::abs(buffer.getReadPointer(ch)[startSample + i]);
                        }
                        sample /= buffer.getNumChannels();

                        weightedSum += sample * i;
                        magnitudeSum += sample;
                    }

                    return magnitudeSum > 0 ? weightedSum / magnitudeSum : 0.0f;
                }

                static float calculateSpectralRolloff(const juce::AudioBuffer<float> &buffer, int startSample, int frameSize)
                {
                    // Simplified spectral rolloff calculation
                    std::vector<float> magnitudes;
                    for (int i = 0; i < frameSize; ++i)
                    {
                        float sample = 0.0f;
                        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        {
                            sample += std::abs(buffer.getReadPointer(ch)[startSample + i]);
                        }
                        magnitudes.push_back(sample / buffer.getNumChannels());
                    }

                    float totalEnergy = std::accumulate(magnitudes.begin(), magnitudes.end(), 0.0f);
                    float threshold = totalEnergy * 0.85f; // 85% of total energy

                    float cumulativeEnergy = 0.0f;
                    for (size_t i = 0; i < magnitudes.size(); ++i)
                    {
                        cumulativeEnergy += magnitudes[i];
                        if (cumulativeEnergy >= threshold)
                            return static_cast<float>(i) / frameSize;
                    }

                    return 1.0f;
                }

                static float detectBPM(const juce::AudioBuffer<float> &buffer, double sampleRate)
                {
                    // Convert to mono if needed
                    juce::AudioBuffer<float> monoBuffer;
                    if (buffer.getNumChannels() > 1)
                    {
                        monoBuffer.setSize(1, buffer.getNumSamples());
                        monoBuffer.copyFrom(0, 0, buffer, 0, 0, buffer.getNumSamples());
                        for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
                        {
                            monoBuffer.addFrom(0, 0, buffer, ch, 0, buffer.getNumSamples());
                        }
                        monoBuffer.applyGain(1.0f / buffer.getNumChannels());
                    }
                    else
                    {
                        monoBuffer.makeCopyOf(buffer);
                    }

                    // Initialize Aubio tempo detection
                    aubio_tempo_t *tempo = new_aubio_tempo("default", WINDOW_SIZE, HOP_SIZE, static_cast<uint_t>(sampleRate));
                    fvec_t *input = new_fvec(HOP_SIZE);
                    fvec_t *output = new_fvec(1);

                    std::vector<float> bpmCandidates;
                    const float *audioData = monoBuffer.getReadPointer(0);

                    // Process audio in chunks
                    for (int i = 0; i < monoBuffer.getNumSamples() - HOP_SIZE; i += HOP_SIZE)
                    {
                        // Copy audio data to aubio vector
                        for (int j = 0; j < HOP_SIZE; ++j)
                        {
                            input->data[j] = audioData[i + j];
                        }

                        // Process the chunk
                        aubio_tempo_do(tempo, input, output);

                        // Check if a beat was detected
                        if (output->data[0] > 0)
                        {
                            float currentBPM = aubio_tempo_get_bpm(tempo);
                            if (currentBPM > 60.0f && currentBPM < 200.0f) // Reasonable BPM range
                            {
                                bpmCandidates.push_back(currentBPM);
                            }
                        }
                    }

                    // Cleanup
                    del_fvec(input);
                    del_fvec(output);
                    del_aubio_tempo(tempo);

                    // Calculate median BPM
                    if (bpmCandidates.empty())
                        return 0.0f;

                    std::sort(bpmCandidates.begin(), bpmCandidates.end());
                    size_t medianIndex = bpmCandidates.size() / 2;
                    return bpmCandidates[medianIndex];
                }

                static std::pair<double, double> detectIntro(const std::vector<EnergyFrame> &frames, double totalDuration)
                {
                    if (frames.empty() || totalDuration < MIN_INTRO_LENGTH)
                        return {0.0, 0.0};

                    // Simple approach: compare first 10% vs middle 10% of track
                    size_t firstSectionEnd = frames.size() / 10;
                    size_t middleStart = frames.size() * 4 / 10;
                    size_t middleEnd = frames.size() * 5 / 10;

                    // Average energy in first 10%
                    float firstEnergy = 0.0f;
                    for (size_t i = 0; i < firstSectionEnd; ++i)
                    {
                        firstEnergy += frames[i].energy;
                    }
                    firstEnergy /= firstSectionEnd;

                    // Average energy in middle 10%
                    float middleEnergy = 0.0f;
                    for (size_t i = middleStart; i < middleEnd; ++i)
                    {
                        middleEnergy += frames[i].energy;
                    }
                    middleEnergy /= (middleEnd - middleStart);

                    // If first section is significantly quieter than middle, it's an intro
                    if (middleEnergy > firstEnergy * 1.5f) // Lowered from 2.0f to 1.5f
                    {
                        // Find where energy crosses the threshold
                        float threshold = firstEnergy + (middleEnergy - firstEnergy) * 0.6f;

                        for (size_t i = 0; i < frames.size() / 3; ++i) // Look in first third
                        {
                            if (frames[i].energy > threshold)
                            {
                                double introEnd = frames[i].timestamp;
                                if (introEnd >= MIN_INTRO_LENGTH)
                                {
                                    return {0.0, introEnd};
                                }
                                break;
                            }
                        }
                    }

                    return {0.0, 0.0};
                }

                static std::pair<double, double> detectOutro(const std::vector<EnergyFrame> &frames, double totalDuration)
                {
                    if (frames.empty() || totalDuration < MIN_OUTRO_LENGTH)
                        return {0.0, 0.0};

                    // More sophisticated approach: look for sustained decline in last 40%
                    size_t analyzeFromIndex = frames.size() * 6 / 10; // Start from 60% into track
                    size_t middleStart = frames.size() * 4 / 10;
                    size_t middleEnd = frames.size() * 5 / 10;

                    // Calculate middle energy (reference point)
                    float middleEnergy = 0.0f;
                    for (size_t i = middleStart; i < middleEnd; ++i)
                    {
                        middleEnergy += frames[i].energy;
                    }
                    middleEnergy /= (middleEnd - middleStart);

                    // Look for a point where energy drops significantly and stays low
                    for (size_t i = analyzeFromIndex; i < frames.size() - 10; ++i) // Leave some buffer at end
                    {
                        // Calculate average energy from this point to end
                        float avgEnergyToEnd = 0.0f;
                        for (size_t j = i; j < frames.size(); ++j)
                        {
                            avgEnergyToEnd += frames[j].energy;
                        }
                        avgEnergyToEnd /= (frames.size() - i);

                        // Check if remaining portion is significantly quieter than middle
                        float ratio = middleEnergy / avgEnergyToEnd;
                        if (ratio >= 1.3f) // Lowered from 1.5f to 1.3f
                        {
                            double outroStart = frames[i].timestamp;
                            double outroLength = totalDuration - outroStart;

                            if (outroLength >= MIN_OUTRO_LENGTH)
                            {
                                return {outroStart, totalDuration};
                            }
                        }
                    }

                    return {0.0, 0.0};
                }

            public:
                static AudioMetadata analyze(const juce::AudioBuffer<float> &buffer, double sampleRate)
                {
                    AudioMetadata metadata;

                    if (buffer.getNumSamples() == 0)
                        return metadata;

                    double totalDuration = static_cast<double>(buffer.getNumSamples()) / sampleRate;

                    // Detect BPM
                    metadata.bpm = detectBPM(buffer, sampleRate);

                    // Calculate energy frames for intro/outro detection
                    std::vector<EnergyFrame> energyFrames = calculateEnergyFrames(buffer, sampleRate);

                    // Detect intro
                    auto [introStart, introEnd] = detectIntro(energyFrames, totalDuration);
                    if (introEnd > introStart)
                    {
                        metadata.hasIntro = true;
                        metadata.introStart = introStart;
                        metadata.introEnd = introEnd;
                    }

                    // Detect outro
                    auto [outroStart, outroEnd] = detectOutro(energyFrames, totalDuration);
                    if (outroEnd > outroStart)
                    {
                        metadata.hasOutro = true;
                        metadata.outroStart = outroStart;
                        metadata.outroEnd = outroEnd;
                    }

                    return metadata;
                }
            };

            AudioMetadata analyzeAudioBuffer(const juce::AudioBuffer<float>& buffer, double sampleRate)
            {
                return AudioAnalyzer::analyze(buffer, sampleRate);
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

                // --- Optimization: Analyze only a portion of the track ---
                const double totalDurationSeconds = reader->lengthInSamples / reader->sampleRate;
                const double analysisDurationSeconds = 60.0; // Analyze 60 seconds
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

                // Perform analysis by calling the new buffer-based function
                metadata = analyzeAudioBuffer(audioBuffer, sampleRate);

                return metadata;
            }
        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio
