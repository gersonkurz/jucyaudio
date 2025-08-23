#pragma once

#include <Database/Includes/EQPreset.h>
#include <optional>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        class IEQPresetManager
        {
        public:
            virtual ~IEQPresetManager() = default;

            // Get all presets (both system and user)
            virtual std::vector<model::EQPreset> getAllPresets() = 0;

            // Save a new preset or update an existing one
            // Returns the saved preset with its ID, or empty if failed
            virtual std::optional<model::EQPreset> savePreset(const juce::String &name, const audio::model::EQSettings &settings) = 0;

            // Delete a user preset (system presets cannot be deleted)
            // Returns true if successful
            virtual bool deletePreset(int64_t presetId) = 0;

            // Get a single preset by ID
            virtual std::optional<model::EQPreset> getPreset(int64_t presetId) = 0;

            // Check if a preset name already exists
            virtual bool presetNameExists(const juce::String &name) = 0;
        };
    } // namespace database
} // namespace jucyaudio