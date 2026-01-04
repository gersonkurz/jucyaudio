#include "AudioVisualizerFIFO.h"

namespace jucyaudio
{
    namespace audio
    {
        AudioVisualizerFIFO::AudioVisualizerFIFO(int capacity)
            : m_fifo(capacity)
            , m_buffer(static_cast<size_t>(capacity))
        {
        }

        int AudioVisualizerFIFO::writeMono(const float* samples, int numSamples)
        {
            const auto scope = m_fifo.write(numSamples);
            if (scope.blockSize1 > 0)
            {
                std::memcpy(m_buffer.data() + scope.startIndex1, samples,
                           static_cast<size_t>(scope.blockSize1) * sizeof(float));
            }
            if (scope.blockSize2 > 0)
            {
                std::memcpy(m_buffer.data() + scope.startIndex2, samples + scope.blockSize1,
                           static_cast<size_t>(scope.blockSize2) * sizeof(float));
            }
            return scope.blockSize1 + scope.blockSize2;
        }

        int AudioVisualizerFIFO::writeStereoAsMono(const float* leftChannel, const float* rightChannel, int numSamples)
        {
            const auto scope = m_fifo.write(numSamples);

            // Write first block
            if (scope.blockSize1 > 0)
            {
                float* dest = m_buffer.data() + scope.startIndex1;
                for (int i = 0; i < scope.blockSize1; ++i)
                {
                    dest[i] = (leftChannel[i] + rightChannel[i]) * 0.5f;
                }
            }

            // Write second block (wrap-around)
            if (scope.blockSize2 > 0)
            {
                float* dest = m_buffer.data() + scope.startIndex2;
                const int offset = scope.blockSize1;
                for (int i = 0; i < scope.blockSize2; ++i)
                {
                    dest[i] = (leftChannel[offset + i] + rightChannel[offset + i]) * 0.5f;
                }
            }

            return scope.blockSize1 + scope.blockSize2;
        }

        int AudioVisualizerFIFO::writeFromBuffer(const juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
        {
            const int numChannels = buffer.getNumChannels();

            if (numChannels == 0 || numSamples <= 0)
                return 0;

            if (numChannels == 1)
            {
                // Mono: write directly
                return writeMono(buffer.getReadPointer(0, startSample), numSamples);
            }
            else
            {
                // Stereo or more: downmix first two channels to mono
                return writeStereoAsMono(
                    buffer.getReadPointer(0, startSample),
                    buffer.getReadPointer(1, startSample),
                    numSamples);
            }
        }

        int AudioVisualizerFIFO::read(float* destBuffer, int numSamples)
        {
            const auto scope = m_fifo.read(numSamples);

            if (scope.blockSize1 > 0)
            {
                std::memcpy(destBuffer, m_buffer.data() + scope.startIndex1,
                           static_cast<size_t>(scope.blockSize1) * sizeof(float));
            }
            if (scope.blockSize2 > 0)
            {
                std::memcpy(destBuffer + scope.blockSize1, m_buffer.data() + scope.startIndex2,
                           static_cast<size_t>(scope.blockSize2) * sizeof(float));
            }

            return scope.blockSize1 + scope.blockSize2;
        }

    } // namespace audio
} // namespace jucyaudio
