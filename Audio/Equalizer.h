#pragma once

#include <Audio/Model/EQSettings.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>

namespace jucyaudio::audio
{
    class Equalizer
    {
    public:
        Equalizer();
        ~Equalizer() = default;
        
        // Prepare the DSP for processing
        void prepare(const juce::dsp::ProcessSpec& spec);
        
        // Process audio block
        void process(juce::dsp::AudioBlock<float>& block);
        
        // Update parameters from settings (thread-safe)
        void updateParameters(const model::EQSettings& settings);
        
        // Reset the DSP state
        void reset();
        
    private:
        // 10-band filter chain + preamp gain
        using Filter = juce::dsp::IIR::Filter<float>;
        using Gain = juce::dsp::Gain<float>;
        
        // We use a ProcessorChain for efficient processing
        juce::dsp::ProcessorChain<
            Gain,     // Preamp gain
            Filter,   // Band 0: 31 Hz
            Filter,   // Band 1: 63 Hz
            Filter,   // Band 2: 125 Hz
            Filter,   // Band 3: 250 Hz
            Filter,   // Band 4: 500 Hz
            Filter,   // Band 5: 1 kHz
            Filter,   // Band 6: 2 kHz
            Filter,   // Band 7: 4 kHz
            Filter,   // Band 8: 8 kHz
            Filter    // Band 9: 16 kHz
        > processorChain;
        
        // Current settings (for thread-safe access)
        model::EQSettings currentSettings;
        std::atomic<bool> parametersChanged{false};
        std::atomic<bool> bypassFlag{false};
        
        // Pending settings (updated from UI thread)
        model::EQSettings pendingSettings;
        juce::SpinLock settingsLock;
        
        // Sample rate for coefficient calculation
        double sampleRate{44100.0};
        
        // Helper to calculate filter coefficients
        void updateFilterCoefficients();
        
        // Process index helpers for clarity
        enum ProcessorIndex
        {
            PreampIndex = 0,
            Band0Index = 1,
            Band1Index = 2,
            Band2Index = 3,
            Band3Index = 4,
            Band4Index = 5,
            Band5Index = 6,
            Band6Index = 7,
            Band7Index = 8,
            Band8Index = 9,
            Band9Index = 10
        };
        
        static constexpr int getBandProcessorIndex(size_t bandIndex)
        {
            return static_cast<int>(bandIndex) + Band0Index;
        }
    };
    
} // namespace jucyaudio::audio