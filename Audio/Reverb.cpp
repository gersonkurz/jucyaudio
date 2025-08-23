#include "Reverb.h"
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace audio
    {
        Reverb::Reverb()
        {
            // Initialize with default settings
            currentSettings = model::ReverbSettings{};
            pendingSettings = currentSettings;
        }

        Reverb::~Reverb() = default;

        void Reverb::prepare(const juce::dsp::ProcessSpec &spec)
        {
            sampleRate = spec.sampleRate;
            reverb.prepare(spec);

            // Apply initial parameters
            applyPendingParameters();

            spdlog::debug("Reverb prepared: sample rate = {}, block size = {}, channels = {}", spec.sampleRate, spec.maximumBlockSize, spec.numChannels);
        }

        void Reverb::process(juce::dsp::AudioBlock<float> &block)
        {
            // Check for bypass
            if (bypassFlag.load(std::memory_order_acquire))
                return;

            // Apply any pending parameter changes
            if (parametersChanged.load(std::memory_order_acquire))
            {
                applyPendingParameters();
                parametersChanged.store(false, std::memory_order_release);
            }

            // Process the audio through the reverb
            juce::dsp::ProcessContextReplacing<float> context(block);
            reverb.process(context);
        }

        void Reverb::reset()
        {
            reverb.reset();
            spdlog::debug("Reverb reset");
        }

        void Reverb::updateParameters(const model::ReverbSettings &settings)
        {
            {
                std::lock_guard<std::mutex> lock(settingsMutex);
                pendingSettings = settings;
                bypassFlag.store(!settings.isActive, std::memory_order_release);
                parametersChanged.store(true, std::memory_order_release);
            }
        }

        model::ReverbSettings Reverb::getCurrentSettings() const
        {
            std::lock_guard<std::mutex> lock(settingsMutex);
            return currentSettings;
        }

        void Reverb::applyPendingParameters()
        {
            model::ReverbSettings settings;
            {
                std::lock_guard<std::mutex> lock(settingsMutex);
                settings = pendingSettings;
                currentSettings = settings;
            }

            // Create JUCE reverb parameters structure
            juce::dsp::Reverb::Parameters params;

            // Map our normalized parameters to JUCE's reverb parameters
            params.roomSize = settings.roomSize;     // 0.0 to 1.0
            params.damping = settings.damping;       // 0.0 to 1.0
            params.wetLevel = settings.wetLevel;     // 0.0 to 1.0
            params.dryLevel = settings.dryLevel;     // 0.0 to 1.0
            params.width = settings.width;           // 0.0 to 1.0
            params.freezeMode = settings.freezeMode; // 0.0 or 1.0

            // Apply to the reverb processor
            reverb.setParameters(params);

            spdlog::trace("Reverb parameters updated: room={:.2f}, damp={:.2f}, wet={:.2f}, dry={:.2f}, width={:.2f}, freeze={}",
                params.roomSize,
                params.damping,
                params.wetLevel,
                params.dryLevel,
                params.width,
                params.freezeMode > 0.5f ? "on" : "off");
        }
    } // namespace audio
} // namespace jucyaudio