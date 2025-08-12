#pragma once

#include <Database/Includes/Constants.h>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        /**
         * @brief Represents album information in the database
         * @details Albums are collections of tracks that share the same album title
         *          within a folder. This struct directly maps to a row in the Albums table.
         */
        struct AlbumInfo final
        {
            /// @brief The unique identifier for this album in the database. Primary Key.
            AlbumId albumId{-1};

            /// @brief The album artist (can be "Various Artists" for compilations)
            std::string albumArtist;

            /// @brief The title of the album (required)
            std::string title;

            /// @brief The release year of the album (optional)
            std::optional<int> year;

            /// @brief The folder_id where this album resides (required)
            /// @note Combined with title forms the unique identification
            FolderId folderId{-1};

            /// @brief Genres associated with the album (stored as JSON array)
            std::vector<std::string> genres;

            /// @brief Moods associated with the album (stored as JSON array)
            std::vector<std::string> moods;

            /// @brief Tags associated with the album (stored as JSON array)
            std::vector<std::string> tags;

            /// @brief Optional Bandcamp URL for the album
            std::string bandcampUrl;

            /// @brief Timestamp when the album was created in the database
            Timestamp_t createdAt;

            /// @brief Timestamp when the album was last updated
            Timestamp_t updatedAt;

            /// @brief Number of tracks associated with this album
            /// @note This is a calculated field, not stored in the database
            int trackCount{0};

            /// @brief Total duration of all tracks in the album
            /// @note This is a calculated field, not stored in the database
            Duration_t totalDuration{0};

            /// @brief A utility function to check if the struct contains valid data from the database
            bool isValid() const
            {
                return albumId >= 0 && !title.empty() && folderId >= 0;
            }

            /// @brief Check if this is a compilation album (Various Artists)
            bool isCompilation() const
            {
                return albumArtist == "Various Artists" || albumArtist == "VA";
            }
        };

    } // namespace database
} // namespace jucyaudio