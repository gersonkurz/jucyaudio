#pragma once

#include <array>
#include <juce_core/juce_core.h>

namespace jucyaudio
{
    namespace audio
    {
        namespace model
        {
            struct EQBandSettings
            {
                float frequency{0.0f};      // Center frequency in Hz
                float gainInDecibels{0.0f}; // Gain in dB (typically -12 to +12)
                float quality{0.707f};      // Q factor (0.707 is a good default for musical EQ)
                bool isActive{true};        // Band enabled/disabled
            };

            struct EQSettings
            {
                static constexpr size_t kBandCount = 10;

                // Standard 10-band frequencies (Hz)
                static constexpr std::array<float, kBandCount> kDefaultFrequencies = {
                    31.0f, 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f};

                std::array<EQBandSettings, kBandCount> bands;
                float preampGain{0.0f}; // Overall gain adjustment in dB
                bool isActive{true};    // Master bypass

                // Initialize with flat response
                EQSettings()
                {
                    for (size_t i = 0; i < kBandCount; ++i)
                    {
                        bands[i].frequency = kDefaultFrequencies[i];
                        bands[i].gainInDecibels = 0.0f;
                        bands[i].quality = 0.707f;
                        bands[i].isActive = true;
                    }
                }

                // Reset to flat response
                void resetToFlat()
                {
                    for (auto &band : bands)
                    {
                        band.gainInDecibels = 0.0f;
                        band.isActive = true;
                    }
                    preampGain = 0.0f;
                    isActive = true;
                }

                // Serialize to JSON string for database storage
                juce::String toJson() const;

                // Deserialize from JSON string
                static EQSettings fromJson(const juce::String &json);
            };
        } // namespace model
    } // namespace audio
} // namespace jucyaudio