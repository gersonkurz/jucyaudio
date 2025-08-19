#pragma once

#include <Database/Includes/ReverbPreset.h>
#include <vector>
#include <optional>
#include <string>

namespace jucyaudio::database
{
    /**
     * Interface for managing reverb presets in the database
     */
    class IReverbPresetManager
    {
    public:
        virtual ~IReverbPresetManager() = default;
        
        /**
         * Get all reverb presets from the database
         * @return Vector of all reverb presets
         */
        virtual std::vector<model::ReverbPreset> getAllPresets() = 0;
        
        /**
         * Get a specific reverb preset by ID
         * @param presetId The ID of the preset to retrieve
         * @return The preset if found, nullopt otherwise
         */
        virtual std::optional<model::ReverbPreset> getPreset(int64_t presetId) = 0;
        
        /**
         * Save a new reverb preset to the database
         * @param name The name for the new preset
         * @param settings The reverb settings to save
         * @return The saved preset with assigned ID if successful, nullopt otherwise
         */
        virtual std::optional<model::ReverbPreset> savePreset(const std::string& name, 
                                                               const audio::model::ReverbSettings& settings) = 0;
        
        /**
         * Delete a reverb preset from the database
         * @param presetId The ID of the preset to delete
         * @return true if successful, false otherwise
         */
        virtual bool deletePreset(int64_t presetId) = 0;
        
        /**
         * Update an existing reverb preset's settings
         * @param presetId The ID of the preset to update
         * @param settings The new reverb settings
         * @return true if successful, false otherwise
         */
        virtual bool updatePreset(int64_t presetId, const audio::model::ReverbSettings& settings) = 0;
        
        /**
         * Check if a preset name already exists
         * @param name The preset name to check
         * @return true if name exists, false otherwise
         */
        virtual bool presetNameExists(const std::string& name) = 0;
    };
    
} // namespace jucyaudio::database