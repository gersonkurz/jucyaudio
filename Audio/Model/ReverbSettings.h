#pragma once

#include <nlohmann/json.hpp>

namespace jucyaudio::audio::model
{
    /**
     * Settings structure for the master reverb effect.
     * All parameters are normalized to 0.0-1.0 range for JUCE's reverb processor.
     */
    struct ReverbSettings
    {
        // Reverb parameters (all 0.0 to 1.0)
        float roomSize = 0.5f;      // Size of the simulated space
        float damping = 0.5f;        // High frequency damping (brightness)
        float wetLevel = 0.33f;      // Level of reverb signal
        float dryLevel = 0.4f;       // Level of direct signal
        float width = 1.0f;          // Stereo width of reverb
        float freezeMode = 0.0f;     // 0=normal, 1=freeze (infinite sustain)
        
        // Master control
        bool isActive = true;        // false = bypass reverb completely
        
        // Default factory settings for different reverb types
        static ReverbSettings smallRoom()
        {
            return {0.2f, 0.7f, 0.25f, 0.75f, 0.8f, 0.0f, true};
        }
        
        static ReverbSettings largeHall()
        {
            return {0.8f, 0.5f, 0.35f, 0.65f, 1.0f, 0.0f, true};
        }
        
        static ReverbSettings cathedral()
        {
            return {0.95f, 0.3f, 0.4f, 0.6f, 1.0f, 0.0f, true};
        }
        
        static ReverbSettings plate()
        {
            return {0.4f, 0.9f, 0.3f, 0.7f, 1.0f, 0.0f, true};
        }
        
        static ReverbSettings spring()
        {
            return {0.3f, 0.6f, 0.35f, 0.65f, 0.5f, 0.0f, true};
        }
        
        static ReverbSettings ambient()
        {
            return {0.85f, 0.2f, 0.5f, 0.5f, 1.0f, 0.0f, true};
        }
        
        static ReverbSettings subtle()
        {
            return {0.15f, 0.8f, 0.15f, 0.85f, 0.7f, 0.0f, true};
        }
        
        // Serialization
        [[nodiscard]] nlohmann::json toJson() const
        {
            return nlohmann::json{
                {"roomSize", roomSize},
                {"damping", damping},
                {"wetLevel", wetLevel},
                {"dryLevel", dryLevel},
                {"width", width},
                {"freezeMode", freezeMode},
                {"isActive", isActive}
            };
        }
        
        static ReverbSettings fromJson(const nlohmann::json& j)
        {
            ReverbSettings settings;
            
            if (j.contains("roomSize"))
                settings.roomSize = j["roomSize"].get<float>();
            if (j.contains("damping"))
                settings.damping = j["damping"].get<float>();
            if (j.contains("wetLevel"))
                settings.wetLevel = j["wetLevel"].get<float>();
            if (j.contains("dryLevel"))
                settings.dryLevel = j["dryLevel"].get<float>();
            if (j.contains("width"))
                settings.width = j["width"].get<float>();
            if (j.contains("freezeMode"))
                settings.freezeMode = j["freezeMode"].get<float>();
            if (j.contains("isActive"))
                settings.isActive = j["isActive"].get<bool>();
                
            return settings;
        }
        
        // Equality comparison
        bool operator==(const ReverbSettings& other) const
        {
            return roomSize == other.roomSize &&
                   damping == other.damping &&
                   wetLevel == other.wetLevel &&
                   dryLevel == other.dryLevel &&
                   width == other.width &&
                   freezeMode == other.freezeMode &&
                   isActive == other.isActive;
        }
        
        bool operator!=(const ReverbSettings& other) const
        {
            return !(*this == other);
        }
    };
    
} // namespace jucyaudio::audio::model