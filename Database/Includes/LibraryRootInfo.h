#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <chrono>

namespace jucyaudio
{
    namespace database
    {
        using LibraryRootId = int64_t;

        /**
         * @brief Represents a single user-defined library root path.
         */
        struct LibraryRootInfo
        {
            LibraryRootId id{0};
            std::string path;
            int64_t fileCount{0};
            std::optional<std::chrono::system_clock::time_point> lastScanned;
        };

    } // namespace database
} // namespace jucyaudio