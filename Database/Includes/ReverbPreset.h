#pragma once

#include <Audio/Model/ReverbSettings.h>
#include <cstdint>
#include <string>

namespace jucyaudio
{
    namespace database
    {
        namespace model
        {
            /**
             * Represents a reverb preset stored in the database
             */
            struct ReverbPreset
            {
                int64_t presetId{-1};                  // Database primary key
                std::string name;                      // User-friendly preset name
                audio::model::ReverbSettings settings; // The actual reverb parameters
                bool isDeletable{true};                // false for factory presets

                // Default constructor
                ReverbPreset() = default;

                // Convenience constructor
                ReverbPreset(int64_t id, const std::string &presetName, const audio::model::ReverbSettings &reverbSettings, bool canDelete = true)
                    : presetId{id},
                      name{presetName},
                      settings{reverbSettings},
                      isDeletable{canDelete}
                {
                }

                // Check if this is a valid preset (has been saved to database)
                [[nodiscard]] bool isValid() const
                {
                    return presetId >= 0;
                }

                // Equality comparison
                bool operator==(const ReverbPreset &other) const
                {
                    return presetId == other.presetId && name == other.name && settings == other.settings && isDeletable == other.isDeletable;
                }

                bool operator!=(const ReverbPreset &other) const
                {
                    return !(*this == other);
                }
            };
        } // namespace model
    } // namespace database
} // namespace jucyaudio