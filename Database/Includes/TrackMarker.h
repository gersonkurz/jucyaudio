#pragma once

#include <Database/Includes/Constants.h>
#include <chrono>
#include <string>
#include <optional>

namespace jucyaudio
{
    namespace database
    {
        using MarkerId = int64_t;
        
        struct TrackMarker
        {
            MarkerId markerId{0};
            TrackId trackId{0};
            std::chrono::milliseconds position{0};
            std::string comment;
            std::chrono::system_clock::time_point createdAt;
            std::chrono::system_clock::time_point updatedAt;
            
            // Optional fields for future enhancement
            std::optional<std::string> color;  // For Phase 2: color coding
            std::optional<std::string> emoji;  // For Phase 2: emoji indicators
        };
    }
}