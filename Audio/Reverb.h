#pragma once

#include <Audio/Model/ReverbSettings.h>
#include <atomic>
#include <juce_dsp/juce_dsp.h>
#include <mutex>

namespace jucyaudio
{
    namespace audio
    {
        /**
         * Master reverb processor for the application.
         * Thread-safe for use in real-time audio context.
         */
        class Reverb
        {
        public:
            Reverb();
            ~Reverb();

            /**
             * Prepare the reverb for processing
             * Must be called before process()
             */
            void prepare(const juce::dsp::ProcessSpec &spec);

            /**
             * Process an audio block through the reverb
             * This is called from the audio thread
             */
            void process(juce::dsp::AudioBlock<float> &block);

            /**
             * Reset the reverb state (clears internal buffers)
             */
            void reset();

            /**
             * Update reverb parameters (thread-safe)
             * Can be called from any thread
             */
            void updateParameters(const model::ReverbSettings &settings);

            /**
             * Get the current reverb settings
             */
            model::ReverbSettings getCurrentSettings() const;

        private:
            // The JUCE reverb processor
            juce::dsp::Reverb reverb;

            // Current sample rate (needed for parameter updates)
            double sampleRate{44100.0};

            // Thread-safe parameter management
            std::atomic<bool> bypassFlag{false};
            std::atomic<bool> parametersChanged{false};

            // Pending settings to be applied on audio thread
            mutable std::mutex settingsMutex;
            model::ReverbSettings pendingSettings;
            model::ReverbSettings currentSettings;

            // Apply pending parameter changes (called on audio thread)
            void applyPendingParameters();
        };
    } // namespace audio
} // namespace jucyaudio