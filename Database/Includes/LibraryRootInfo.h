#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <chrono>

#include <Database/Includes/FolderInfo.h>

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
            std::optional<std::chrono::system_clock::time_point> lastScanned;

            // output only
            FolderInfo folderInfo;
        };

    } // namespace database
} // namespace jucyaudio