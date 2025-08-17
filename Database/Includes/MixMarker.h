#pragma once

#include <Database/Includes/Constants.h>
#include <string>
#include <chrono>
#include <optional>

namespace jucyaudio::database
{
    struct MixMarker
    {
        MarkerId marker_id{0};
        MixId mix_id{0};
        std::chrono::milliseconds position{0};
        std::string comment;
        std::optional<std::string> color;  // Optional hex color (e.g., "#FF5722")
        std::optional<std::string> emoji;  // Optional emoji
        std::chrono::system_clock::time_point created_at;
        std::chrono::system_clock::time_point updated_at;
    };
}