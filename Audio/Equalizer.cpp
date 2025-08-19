#include "Equalizer.h"
#include <spdlog/spdlog.h>

namespace jucyaudio::audio
{
    Equalizer::Equalizer()
    {
        // Initialize with flat response
        currentSettings.resetToFlat();
        pendingSettings = currentSettings;
    }
    
    void Equalizer::prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        processorChain.prepare(spec);
        
        // Set up initial filter coefficients
        updateFilterCoefficients();
        
        spdlog::info("Equalizer prepared: sample rate = {}, block size = {}, channels = {}", 
                     spec.sampleRate, spec.maximumBlockSize, spec.numChannels);
    }
    
    void Equalizer::process(juce::dsp::AudioBlock<float>& block)
    {
        // Check for bypass
        if (bypassFlag.load(std::memory_order_acquire))
            return;
        
        // Check if parameters need updating (on audio thread)
        if (parametersChanged.exchange(false, std::memory_order_acq_rel))
        {
            juce::SpinLock::ScopedLockType lock(settingsLock);
            currentSettings = pendingSettings;
            updateFilterCoefficients();
        }
        
        // Process the audio through the chain
        juce::dsp::ProcessContextReplacing<float> context(block);
        processorChain.process(context);
    }
    
    void Equalizer::updateParameters(const model::EQSettings& settings)
    {
        // Called from UI thread - update pending settings
        {
            juce::SpinLock::ScopedTryLockType lock(settingsLock);
            if (lock.isLocked())
            {
                pendingSettings = settings;
                bypassFlag.store(!settings.isActive, std::memory_order_release);
                parametersChanged.store(true, std::memory_order_release);
            }
        }
    }
    
    void Equalizer::reset()
    {
        processorChain.reset();
    }
    
    void Equalizer::updateFilterCoefficients()
    {
        // Update preamp gain
        auto& preamp = processorChain.get<PreampIndex>();
        preamp.setGainDecibels(currentSettings.preampGain);
        
        // Helper lambda to update a band's filter coefficients
        auto updateBandCoefficients = [this](auto& filter, const model::EQBandSettings& band)
        {
            if (band.isActive && std::abs(band.gainInDecibels) > 0.01f)
            {
                // Calculate peak filter coefficients
                *filter.coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                    sampleRate,
                    band.frequency,
                    band.quality,
                    juce::Decibels::decibelsToGain(band.gainInDecibels)
                );
            }
            else
            {
                // Bypass this band by setting it to unity gain (all-pass)
                *filter.coefficients = *juce::dsp::IIR::Coefficients<float>::makeAllPass(
                    sampleRate, 
                    band.frequency, 
                    band.quality
                );
            }
        };
        
        // Update each band's filter coefficients explicitly
        updateBandCoefficients(processorChain.get<Band0Index>(), currentSettings.bands[0]);
        updateBandCoefficients(processorChain.get<Band1Index>(), currentSettings.bands[1]);
        updateBandCoefficients(processorChain.get<Band2Index>(), currentSettings.bands[2]);
        updateBandCoefficients(processorChain.get<Band3Index>(), currentSettings.bands[3]);
        updateBandCoefficients(processorChain.get<Band4Index>(), currentSettings.bands[4]);
        updateBandCoefficients(processorChain.get<Band5Index>(), currentSettings.bands[5]);
        updateBandCoefficients(processorChain.get<Band6Index>(), currentSettings.bands[6]);
        updateBandCoefficients(processorChain.get<Band7Index>(), currentSettings.bands[7]);
        updateBandCoefficients(processorChain.get<Band8Index>(), currentSettings.bands[8]);
        updateBandCoefficients(processorChain.get<Band9Index>(), currentSettings.bands[9]);
        
        // Set bypass state for each band
        processorChain.setBypassed<Band0Index>(!currentSettings.bands[0].isActive);
        processorChain.setBypassed<Band1Index>(!currentSettings.bands[1].isActive);
        processorChain.setBypassed<Band2Index>(!currentSettings.bands[2].isActive);
        processorChain.setBypassed<Band3Index>(!currentSettings.bands[3].isActive);
        processorChain.setBypassed<Band4Index>(!currentSettings.bands[4].isActive);
        processorChain.setBypassed<Band5Index>(!currentSettings.bands[5].isActive);
        processorChain.setBypassed<Band6Index>(!currentSettings.bands[6].isActive);
        processorChain.setBypassed<Band7Index>(!currentSettings.bands[7].isActive);
        processorChain.setBypassed<Band8Index>(!currentSettings.bands[8].isActive);
        processorChain.setBypassed<Band9Index>(!currentSettings.bands[9].isActive);
    }
    
} // namespace jucyaudio::audio