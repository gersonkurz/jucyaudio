#pragma once

#include <Database/Includes/IAlbumManager.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>

namespace jucyaudio
{
    namespace database
    {
        /**
         * @brief SQLite implementation of the IAlbumManager interface
         * @details Manages album data in the SQLite database, including
         *          creation, querying, and association with tracks.
         */
        class SqliteAlbumManager : public IAlbumManager
        {
        public:
            explicit SqliteAlbumManager(SqliteDatabase& db)
                : m_db{db}
            {
            }
            ~SqliteAlbumManager() override = default;

            // IAlbumManager interface implementation
            AlbumId findOrCreateAlbum(
                const std::string& title,
                FolderId folderId,
                const std::string& albumArtist = "",
                std::optional<int> year = std::nullopt) override;

            std::optional<AlbumInfo> getAlbumById(AlbumId albumId) const override;
            std::vector<AlbumInfo> getAlbumsInFolder(FolderId folderId) const override;
            std::vector<AlbumInfo> getAllAlbums(size_t limit = 0, size_t offset = 0) const override;
            
            bool updateAlbumMetadata(
                AlbumId albumId,
                const std::vector<std::string>& genres,
                const std::vector<std::string>& moods,
                const std::vector<std::string>& tags) override;
            
            bool updateAlbumBandcampUrl(AlbumId albumId, const std::string& url) override;
            bool updateAlbumBitrate(AlbumId albumId, std::optional<int> bitrate) override;
            bool setTrackAlbum(TrackId trackId, AlbumId albumId) override;
            std::vector<TrackId> getAlbumTracks(AlbumId albumId) const override;
            bool deleteAlbum(AlbumId albumId) override;
            std::vector<AlbumInfo> searchAlbums(const std::string& query, size_t limit = 100) const override;

        private:
            SqliteDatabase& m_db;

            /// @brief Helper to convert JSON string to vector
            std::vector<std::string> jsonArrayToVector(const std::string& json) const;
            
            /// @brief Helper to convert vector to JSON string
            std::string vectorToJsonArray(const std::vector<std::string>& vec) const;
            
            /// @brief Helper to create AlbumInfo from SQLite statement
            AlbumInfo albumInfoFromStatement(const SqliteStatement& stmt) const;
        };

    } // namespace database
} // namespace jucyaudio