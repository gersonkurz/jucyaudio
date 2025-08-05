#pragma once

#include <cstdint>
#include <string>

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
        };

    } // namespace database
} // namespace jucyaudio