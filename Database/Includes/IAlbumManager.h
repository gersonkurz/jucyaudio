#pragma once

#include <Database/Includes/AlbumInfo.h>
#include <Database/Includes/Constants.h>
#include <Database/Includes/TrackInfo.h>
#include <optional>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        /**
         * @brief Interface for managing albums in the database
         * @details Provides methods for creating, updating, and querying albums.
         *          Albums are identified by the combination of title and folder_id.
         */
        /**
         * @brief A genre from the controlled vocabulary, with how many albums currently carry it.
         * @note The count drives the colour banding in the genre picker; it is not persisted.
         */
        struct GenreUsage final
        {
            std::string name;
            int albumCount{0};
        };

        class IAlbumManager
        {
        public:
            virtual ~IAlbumManager() = default;

            /// @brief Find or create an album with the given title and folder
            /// @param title The album title
            /// @param folderId The folder where the album resides
            /// @param albumArtist Optional album artist (can be "Various Artists")
            /// @param year Optional release year
            /// @return The album ID (existing or newly created)
            virtual AlbumId findOrCreateAlbum(
                const std::string& title,
                FolderId folderId,
                const std::string& albumArtist = "",
                std::optional<int> year = std::nullopt) = 0;

            /// @brief Get album information by ID
            /// @param albumId The album ID to query
            /// @return Album info if found, nullopt otherwise
            virtual std::optional<AlbumInfo> getAlbumById(AlbumId albumId) const = 0;

            /// @brief Get all albums in a specific folder
            /// @param folderId The folder to query
            /// @return Vector of albums in the folder
            virtual std::vector<AlbumInfo> getAlbumsInFolder(FolderId folderId) const = 0;

            /// @brief Get all albums in the database
            /// @param limit Maximum number of albums to return (0 = no limit)
            /// @param offset Number of albums to skip
            /// @return Vector of all albums
            virtual std::vector<AlbumInfo> getAllAlbums(size_t limit = 0, size_t offset = 0) const = 0;

            /// @brief Update album metadata (genres, moods, tags)
            /// @param albumId The album to update
            /// @param genres New genre list (replaces existing)
            /// @param moods New mood list (replaces existing)
            /// @param tags New tag list (replaces existing)
            /// @return true if successful
            virtual bool updateAlbumMetadata(
                AlbumId albumId,
                const std::vector<std::string>& genres,
                const std::vector<std::string>& moods,
                const std::vector<std::string>& tags) = 0;

            /// @brief Update album's Bandcamp URL
            /// @param albumId The album to update
            /// @param url The Bandcamp URL (empty string to clear)
            /// @return true if successful
            virtual bool updateAlbumBandcampUrl(AlbumId albumId, const std::string& url) = 0;

            /// @brief Update album's bitrate
            /// @param albumId The album to update
            /// @param bitrate The average bitrate in kbps (nullopt to clear)
            /// @return true if successful
            virtual bool updateAlbumBitrate(AlbumId albumId, std::optional<int> bitrate) = 0;

            /// @brief Associate a track with an album
            /// @param trackId The track to update
            /// @param albumId The album to associate (or -1 to clear)
            /// @return true if successful
            virtual bool setTrackAlbum(TrackId trackId, AlbumId albumId) = 0;

            /// @brief Get all tracks associated with an album
            /// @param albumId The album to query
            /// @return Vector of track IDs
            virtual std::vector<TrackId> getAlbumTracks(AlbumId albumId) const = 0;

            /// @brief Delete an album from the database
            /// @param albumId The album to delete
            /// @return true if successful
            virtual bool deleteAlbum(AlbumId albumId) = 0;

            /// @brief Get the genre vocabulary, each entry annotated with how many albums use it
            /// @return Vocabulary in alphabetical order
            virtual std::vector<GenreUsage> getGenresWithUsage() const = 0;

            /// @brief Add a name to the genre vocabulary
            /// @param name The genre name; ignored if it already exists (case-insensitively)
            /// @return true if the vocabulary now contains the name
            virtual bool addGenre(const std::string& name) = 0;

            /// @brief Search albums by text query
            /// @param query Search text (searches title, artist, genres, tags)
            /// @param limit Maximum results to return
            /// @return Vector of matching albums
            virtual std::vector<AlbumInfo> searchAlbums(const std::string& query, size_t limit = 100) const = 0;
        };

    } // namespace database
} // namespace jucyaudio