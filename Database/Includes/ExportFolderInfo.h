#pragma once

#include <string>
#include <chrono>

namespace jucyaudio
{
    namespace database
    {
        struct ExportFolderInfo
        {
            int folderId{0};
            std::string name;
            int displayOrder{0};
            std::chrono::system_clock::time_point createdAt;
            std::string description;
        };
    }
}