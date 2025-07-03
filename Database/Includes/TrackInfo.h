#pragma once

#include <chrono>     // For time_point
#include <cstdint>    // For std::uintmax_t
#include <filesystem> // For file_size return type (uintmax_t)
#include <string>
#include <vector>
#include <Database/Includes/Constants.h>

namespace jucyaudio
{
    namespace database
    {
        struct TrackInfo
        {
            TrackId trackId = -1;
            FolderId folderId = -1; // Folder ID this track belongs to, -1 if not set
            std::filesystem::path filepath; // Path to the audio file

            // Filesystem attributes
            Timestamp_t last_modified_fs;
            std::uintmax_t filesize_bytes = 0; // Correct type for file_size

            // Library metadata
            Timestamp_t date_added;
            Timestamp_t last_scanned;

            // Core ID3-like Metadata
            std::string title;
            std::string artist_name;
            std::string album_title;
            std::string album_artist_name;
            int track_number = 0;
            int disc_number = 0;
            int year = 0;
            std::vector<TagId> tag_ids;

            // Audio Properties
            Duration_t duration{0};
            int samplerate = 0;
            int channels = 0;
            int bitrate = 0;
            std::string codec_name;

            // Analysis Results
            BPM_t bpm = 0;
            std::string key_string;
            std::string beat_locations_json; // Or a more structured representation

            // User Data
            int rating = 0;
            int liked_status = 0; // 0 = neutral, 1 = liked, -1 = disliked
            int play_count = 0;
            Timestamp_t last_played;

            std::string internal_content_hash; // Optional
            std::string user_notes;
            bool is_missing = false; // True if file not found on disk during last scan
        };

    } // namespace database
} // namespace jucyaudio