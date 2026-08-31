#pragma once

#include <Database/Includes/Constants.h>
#include <chrono>     // For time_point
#include <cstdint>    // For std::uintmax_t
#include <filesystem> // For file_size return type (uintmax_t)
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        class ITrackDatabase;

        /**
         * @brief Defines the status of a track, primarily for handling files that cannot be decoded.
         */
        enum class TrackStatus
        {
            Unknown,  ///< The track has never been analyzed or played. This is the default state.
            Ok,       ///< The track has been successfully decoded (for playback or analysis).
            BadFormat ///< An attempt to decode the track failed; it is likely corrupted or in an unsupported format.
        };

        /**
         * @brief A comprehensive data structure representing a single audio track in the library.
         * This struct holds all metadata, from filesystem attributes to audio analysis results and user data.
         */
        /**
         * @brief Which parts of a track a scan actually established.
         *
         * A bitmask rather than a yes-or-no, because a read can half succeed and the two halves come
         * from different places. TagLib will hand back a tag object for a file whose audio properties it
         * cannot work out, and the reverse happens too. A single "did it work" answer forces a choice
         * between discarding tags that were read and writing a zero duration over a real one.
         *
         * What is *not* in here: the location columns - folder, filename, size, modification time. The
         * scanner reads those from the directory entry rather than from the file, so they are known
         * whenever there is a file to talk about at all, and updateScannedTrackData always writes them.
         */
        enum class ScannedFields : unsigned
        {
            None = 0,

            /// @brief title, artist_name, album_title, track_number, year, and the genre list.
            Tags = 1u << 0,

            /// @brief duration, samplerate, channels, bitrate.
            AudioProperties = 1u << 1,

            Everything = Tags | AudioProperties
        };

        constexpr ScannedFields operator|(ScannedFields a, ScannedFields b)
        {
            return static_cast<ScannedFields>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
        }

        constexpr ScannedFields &operator|=(ScannedFields &a, ScannedFields b)
        {
            a = a | b;
            return a;
        }

        /// @brief Does this set include everything in @p what?
        constexpr bool includes(ScannedFields set, ScannedFields what)
        {
            return (static_cast<unsigned>(set) & static_cast<unsigned>(what)) == static_cast<unsigned>(what);
        }

        struct TrackInfo
        {
            /// @brief The unique identifier for this track in the database. Primary Key.
            TrackId trackId = -1;
            /// @brief The ID of the immediate parent folder in the hierarchical `Folders` table. Foreign Key.
            FolderId folderId = -1;
            /// @brief The track's filename, including the extension (e.g., "Song Title.mp3").
            std::string filename;

            /// @brief Reconstructs the full path to the track by combining the folder path and filename.
            std::filesystem::path reconstructFullPath() const;
            /**
             * @brief Reconstructs the full path to the track using the provided database connection.
             * @param db A reference to the database to use for reconstructing the folder path.
             * @return The full, absolute path to the track file.
             */
            std::filesystem::path reconstructFullPath(const ITrackDatabase &db) const;

            // Filesystem attributes
            /// @brief The timestamp of the last modification of the file on disk.
            Timestamp_t last_modified_fs;
            /// @brief The size of the audio file in bytes.
            std::uintmax_t filesize_bytes = 0;

            // Library metadata
            /// @brief The timestamp when the track was first added to the library.
            Timestamp_t date_added;
            /// @brief The timestamp of the last time this track's metadata was updated by a library scan.
            Timestamp_t last_scanned;

            // Core ID3-like Metadata
            /// @brief The title of the track.
            std::string title;
            /// @brief The primary artist of the track.
            std::string artist_name;
            /// @brief The title of the album the track belongs to.
            std::string album_title;
            /// @brief The primary artist of the album.
            std::string album_artist_name;
            /// @brief The track number within its album.
            int track_number = 0;
            /// @brief The disc number if the album has multiple discs.
            int disc_number = 0;
            /// @brief The year the track was released.
            int year = 0;
            /// @brief A vector of Tag IDs associated with this track.
            std::vector<TagId> tag_ids;
            /// @brief The album ID this track belongs to (optional, -1 if not associated)
            AlbumId albumId = -1;

            // Audio Properties
            /// @brief The total duration of the audio file.
            Duration_t duration{0};
            /// @brief The sample rate of the audio in Hz (e.g., 44100).
            int samplerate = 0;
            /// @brief The number of audio channels (e.g., 1 for mono, 2 for stereo).
            int channels = 0;
            /// @brief The bitrate of the audio in kbps.
            int bitrate = 0;
            /// @brief The name of the audio codec (e.g., "MP3", "FLAC").
            std::string codec_name;

            // Analysis Results
            /// @brief The detected Beats Per Minute (BPM) of the track, if available.
            std::optional<BPM_t> bpm;
            /// @brief The timestamp marking the end of the track's intro, relative to the start.
            std::optional<Duration_t> intro_end;
            /// @brief The timestamp marking the start of the track's outro, relative to the start.
            std::optional<Duration_t> outro_start;
            /// @brief The musical key of the track (e.g., "C#m").
            std::string key_string;
            /// @brief A JSON string representing the locations of beats within the track.
            std::string beat_locations_json;

            // User Data
            /// @brief The timestamp of the last time the track was played.
            Timestamp_t last_played;

            /// @brief An optional hash of the audio content to detect duplicates.
            std::string internal_content_hash;
            /// @brief A flag indicating if the file was not found on disk during the last library verification scan.
            bool is_missing = false;

            /// @brief The decoding status of the track.
            TrackStatus status = TrackStatus::Unknown;
        };

    } // namespace database
} // namespace jucyaudio