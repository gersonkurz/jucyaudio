#pragma once

#include <filesystem>

namespace jucyaudio
{
    namespace audio
    {
        struct ActiveExportSettings
        {
            std::filesystem::path outputPath;

            // ID3 tags for MP3 export
            std::string artist;
            std::string album;
            std::string title;
            std::string year;
            std::string genre;
            std::string comment;
        };
    } // namespace audio
} // namespace jucyaudio
