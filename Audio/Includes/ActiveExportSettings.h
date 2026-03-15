#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>

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

namespace nlohmann
{
    template <> struct adl_serializer<jucyaudio::audio::ActiveExportSettings>
    {
        static void to_json(json& j, const jucyaudio::audio::ActiveExportSettings& s)
        {
            j = json{
                {"outputPath", s.outputPath.string()},
                {"exportFolder", s.exportFolder},
                {"artist", s.artist},
                {"album", s.album},
                {"title", s.title},
                {"trackNumber", s.trackNumber},
                {"year", s.year},
                {"genre", s.genre},
                {"comment", s.comment}
            };
        }

        static void from_json(const json& j, jucyaudio::audio::ActiveExportSettings& s)
        {
            s.outputPath = j.value("outputPath", std::string{});
            s.exportFolder = j.value("exportFolder", std::string{});
            s.artist = j.value("artist", std::string{});
            s.album = j.value("album", std::string{});
            s.title = j.value("title", std::string{});
            s.trackNumber = j.value("trackNumber", std::string{});
            s.year = j.value("year", std::string{});
            s.genre = j.value("genre", std::string{});
            s.comment = j.value("comment", std::string{});
        }
    };
} // namespace nlohmann
