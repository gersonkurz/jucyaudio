#pragma once

#include <filesystem>

namespace jucyaudio
{
    namespace audio
    {
        struct ActiveExportSettings
        {
            std::filesystem::path outputPath;
            std::string exportFolder; // Which export folder this belongs to

            // ID3 tags for MP3 export
            std::string artist;
            std::string album;
            std::string title;
            std::string trackNumber; // Track number (e.g., "1" or "1/12" for track 1 of 12)
            std::string year;
            std::string genre;
            std::string comment;
        };
    } // namespace audio
} // namespace jucyaudio
