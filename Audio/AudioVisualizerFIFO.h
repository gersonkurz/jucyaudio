#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

namespace jucyaudio
{
    namespace audio
    {
        /**
         * @brief Lock-free Single Producer, Single Consumer FIFO for audio visualization.
         *
         * This class provides a thread-safe way to pass audio data from the audio thread
         * to the visualization render thread without blocking.
         *
         * Usage:
         * - Audio thread calls writeMono() or writeStereoAsMono() after mixing
         * - Render thread calls read() to get samples for visualization
         *
         * The buffer stores mono float samples. Stereo input is downmixed to mono.
         */
        class AudioVisualizerFIFO
        {
        public:
            /**
             * @brief Construct a FIFO with the specified capacity.
             * @param capacity Number of samples the buffer can hold (default 2048)
             */
            explicit AudioVisualizerFIFO(int capacity = 2048);

            /**
             * @brief Write mono samples to the FIFO (audio thread).
             * @param samples Pointer to mono float samples
             * @param numSamples Number of samples to write
             * @return Number of samples actually written (may be less if buffer full)
             */
            int writeMono(const float* samples, int numSamples);

            /**
             * @brief Write stereo samples as mono downmix to the FIFO (audio thread).
             * @param leftChannel Pointer to left channel samples
             * @param rightChannel Pointer to right channel samples
             * @param numSamples Number of samples per channel
             * @return Number of samples actually written (may be less if buffer full)
             */
            int writeStereoAsMono(const float* leftChannel, const float* rightChannel, int numSamples);

            /**
             * @brief Write from a JUCE AudioBuffer (audio thread).
             * Automatically handles mono/stereo conversion.
             * @param buffer The audio buffer to read from
             * @param startSample Starting sample index in the buffer
             * @param numSamples Number of samples to write
             * @return Number of samples actually written
             */
            int writeFromBuffer(const juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

            /**
             * @brief Read samples from the FIFO (render thread).
             * @param destBuffer Destination buffer for samples
             * @param numSamples Maximum number of samples to read
             * @return Number of samples actually read
             */
            int read(float* destBuffer, int numSamples);

            /**
             * @brief Get the number of samples available for reading.
             */
            int getNumReady() const { return m_fifo.getNumReady(); }

            /**
             * @brief Get the free space available for writing.
             */
            int getFreeSpace() const { return m_fifo.getFreeSpace(); }

            /**
             * @brief Clear all data from the FIFO.
             */
            void reset() { m_fifo.reset(); }

            /**
             * @brief Set the sample rate (for visualization timing calculations).
             */
            void setSampleRate(double sampleRate) { m_sampleRate.store(sampleRate); }

            /**
             * @brief Get the current sample rate.
             */
            double getSampleRate() const { return m_sampleRate.load(); }

        private:
            juce::AbstractFifo m_fifo;
            std::vector<float> m_buffer;
            std::atomic<double> m_sampleRate{48000.0};

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioVisualizerFIFO)
        };

    } // namespace audio
} // namespace jucyaudio
