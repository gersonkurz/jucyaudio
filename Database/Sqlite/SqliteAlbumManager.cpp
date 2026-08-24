#include <Database/Sqlite/SqliteAlbumManager.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Database/TrackLibrary.h>
#include <UI/Settings.h>
#include <Utils/AssortedUtils.h>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        AlbumId SqliteAlbumManager::findOrCreateAlbum(
            const std::string& title,
            FolderId folderId,
            const std::string& albumArtist,
            std::optional<int> year)
        {
            if (title.empty() || folderId < 0)
            {
                spdlog::error("Invalid album parameters: title='{}', folderId={}", title, folderId);
                return -1;
            }

            // First, try to find existing album
            const char* findSql = "SELECT album_id FROM Albums WHERE title = ? AND folder_id = ?;";
            SqliteStatement findStmt{m_db, findSql};
            findStmt.addParam(title);
            findStmt.addParam(folderId);

            if (findStmt.getNextResult())
            {
                return findStmt.getInt64(0);
            }

            // Album doesn't exist, create it
            const char* insertSql = R"SQL(
                INSERT INTO Albums (album_artist, title, year, folder_id)
                VALUES (?, ?, ?, ?);
            )SQL";
            
            SqliteStatement insertStmt{m_db, insertSql};
            insertStmt.addNullableParam(albumArtist.empty() ? std::nullopt : std::optional(albumArtist));
            insertStmt.addParam(title);
            insertStmt.addNullableParam(year);
            insertStmt.addParam(folderId);

            if (insertStmt.execute())
            {
                const auto albumId = m_db.getLastInsertRowId();
                spdlog::debug("Created new album '{}' with ID {} in folder {}", title, albumId, folderId);
                return albumId;
            }

            spdlog::error("Failed to create album '{}': {}", title, m_db.getLastError());
            return -1;
        }

        std::optional<AlbumInfo> SqliteAlbumManager::getAlbumById(AlbumId albumId) const
        {
            const char* sql = R"SQL(
                SELECT album_id, album_artist, title, year, folder_id, 
                       genres, moods, tags, bandcamp_url, bitrate, created_at, updated_at
                FROM Albums WHERE album_id = ?;
            )SQL";
            
            SqliteStatement stmt{m_db, sql};
            stmt.addParam(albumId);

            if (stmt.getNextResult())
            {
                return albumInfoFromStatement(stmt);
            }

            return std::nullopt;
        }

        std::vector<AlbumInfo> SqliteAlbumManager::getAlbumsInFolder(FolderId folderId) const
        {
            std::vector<AlbumInfo> albums;
            
            const char* sql = R"SQL(
                SELECT album_id, album_artist, title, year, folder_id, 
                       genres, moods, tags, bandcamp_url, bitrate, created_at, updated_at
                FROM Albums WHERE folder_id = ?
                ORDER BY title COLLATE NOCASE;
            )SQL";
            
            SqliteStatement stmt{m_db, sql};
            stmt.addParam(folderId);

            while (stmt.getNextResult())
            {
                albums.push_back(albumInfoFromStatement(stmt));
            }

            return albums;
        }

        std::vector<AlbumInfo> SqliteAlbumManager::getAllAlbums(size_t limit, size_t offset) const
        {
            std::vector<AlbumInfo> albums;
            
            // Check if we should filter offline albums
            std::string sql;
            if (!config::theSettings.uiSettings.showOfflineTracks)
            {
                // Filter out albums from offline folders
                sql = R"SQL(
                    SELECT album_id, album_artist, title, year, folder_id, 
                           genres, moods, tags, bandcamp_url, bitrate, created_at, updated_at
                    FROM Albums 
                    WHERE folder_id NOT IN (SELECT folder_id FROM temp.OfflineFolders)
                    ORDER BY album_artist COLLATE NOCASE, title COLLATE NOCASE
                )SQL";
            }
            else
            {
                // Show all albums
                sql = R"SQL(
                    SELECT album_id, album_artist, title, year, folder_id, 
                           genres, moods, tags, bandcamp_url, bitrate, created_at, updated_at
                    FROM Albums 
                    ORDER BY album_artist COLLATE NOCASE, title COLLATE NOCASE
                )SQL";
            }
            
            if (limit > 0)
            {
                sql += " LIMIT " + std::to_string(limit);
                if (offset > 0)
                {
                    sql += " OFFSET " + std::to_string(offset);
                }
            }
            sql += ";";
            
            SqliteStatement stmt{m_db, sql};

            while (stmt.getNextResult())
            {
                albums.push_back(albumInfoFromStatement(stmt));
            }

            return albums;
        }

        bool SqliteAlbumManager::updateAlbumMetadata(
            AlbumId albumId,
            const std::vector<std::string>& genres,
            const std::vector<std::string>& moods,
            const std::vector<std::string>& tags)
        {
            const char* sql = R"SQL(
                UPDATE Albums 
                SET genres = ?, moods = ?, tags = ?, updated_at = CURRENT_TIMESTAMP
                WHERE album_id = ?;
            )SQL";
            
            SqliteStatement stmt{m_db, sql};
            stmt.addParam(vectorToJsonArray(genres));
            stmt.addParam(vectorToJsonArray(moods));
            stmt.addParam(vectorToJsonArray(tags));
            stmt.addParam(albumId);

            if (stmt.execute())
            {
                spdlog::info("Updated metadata for album {}", albumId);
                return true;
            }

            spdlog::error("Failed to update metadata for album {}: {}", albumId, m_db.getLastError());
            return false;
        }

        bool SqliteAlbumManager::updateAlbumBandcampUrl(AlbumId albumId, const std::string& url)
        {
            const char* sql = R"SQL(
                UPDATE Albums 
                SET bandcamp_url = ?, updated_at = CURRENT_TIMESTAMP
                WHERE album_id = ?;
            )SQL";
            
            SqliteStatement stmt{m_db, sql};
            stmt.addParam(url);
            stmt.addParam(albumId);

            return stmt.execute();
        }

        bool SqliteAlbumManager::updateAlbumBitrate(AlbumId albumId, std::optional<int> bitrate)
        {
            const char* sql = R"SQL(
                UPDATE Albums 
                SET bitrate = ?, updated_at = CURRENT_TIMESTAMP
                WHERE album_id = ?;
            )SQL";
            
            SqliteStatement stmt{m_db, sql};
            stmt.addNullableParam(bitrate);
            stmt.addParam(albumId);

            return stmt.execute();
        }

        bool SqliteAlbumManager::setTrackAlbum(TrackId trackId, AlbumId albumId)
        {
            const char* sql = "UPDATE Tracks SET album_id = ? WHERE track_id = ?;";
            
            SqliteStatement stmt{m_db, sql};
            stmt.addNullableParam(albumId < 0 ? std::nullopt : std::optional(albumId));
            stmt.addParam(trackId);

            if (stmt.execute())
            {
                spdlog::debug("Set album {} for track {}", albumId, trackId);
                return true;
            }

            spdlog::error("Failed to set album for track {}: {}", trackId, m_db.getLastError());
            return false;
        }

        std::vector<TrackId> SqliteAlbumManager::getAlbumTracks(AlbumId albumId) const
        {
            std::vector<TrackId> trackIds;
            
            const char* sql = "SELECT track_id FROM Tracks WHERE album_id = ? ORDER BY disc_number, track_number;";
            
            SqliteStatement stmt{m_db, sql};
            stmt.addParam(albumId);

            while (stmt.getNextResult())
            {
                trackIds.push_back(stmt.getInt64(0));
            }

            return trackIds;
        }

        bool SqliteAlbumManager::deleteAlbum(AlbumId albumId)
        {
            SqliteTransaction transaction{m_db};
            
            // First, clear album_id from tracks
            const char* clearTracksSql = "UPDATE Tracks SET album_id = NULL WHERE album_id = ?;";
            SqliteStatement clearStmt{m_db, clearTracksSql};
            clearStmt.addParam(albumId);
            
            if (!clearStmt.execute())
            {
                transaction.rollback();
                return false;
            }

            // Then delete the album
            const char* deleteSql = "DELETE FROM Albums WHERE album_id = ?;";
            SqliteStatement deleteStmt{m_db, deleteSql};
            deleteStmt.addParam(albumId);
            
            if (!deleteStmt.execute())
            {
                transaction.rollback();
                return false;
            }

            return transaction.commit();
        }

        std::vector<AlbumInfo> SqliteAlbumManager::searchAlbums(const std::string& query, size_t limit) const
        {
            std::vector<AlbumInfo> albums;
            
            // For now, simple LIKE search. Later can integrate with FTS5
            const char* sql = R"SQL(
                SELECT album_id, album_artist, title, year, folder_id, 
                       genres, moods, tags, bandcamp_url, bitrate, created_at, updated_at
                FROM Albums 
                WHERE title LIKE ? OR album_artist LIKE ? OR genres LIKE ? OR tags LIKE ?
                ORDER BY album_artist COLLATE NOCASE, title COLLATE NOCASE
                LIMIT ?;
            )SQL";
            
            std::string searchPattern = "%" + query + "%";
            SqliteStatement stmt{m_db, sql};
            stmt.addParam(searchPattern);
            stmt.addParam(searchPattern);
            stmt.addParam(searchPattern);
            stmt.addParam(searchPattern);
            stmt.addParam(static_cast<int64_t>(limit));

            while (stmt.getNextResult())
            {
                albums.push_back(albumInfoFromStatement(stmt));
            }

            return albums;
        }

        // Helper methods
        std::vector<GenreUsage> SqliteAlbumManager::getGenresWithUsage() const
        {
            std::vector<GenreUsage> genres;

            {
                SqliteStatement stmt{m_db, "SELECT name FROM Genres ORDER BY name COLLATE NOCASE;"};
                while (stmt.getNextResult())
                {
                    genres.push_back(GenreUsage{stmt.getText(0), 0});
                }
            }

            if (genres.empty())
            {
                return genres;
            }

            // Count usage by walking the albums that actually carry a genre. Albums.genres is a JSON
            // array, so this cannot be a GROUP BY; but only labelled albums are read, which is a small
            // fraction of the table and keeps the vocabulary and the counts in one round trip.
            std::unordered_map<std::string, int> counts;
            {
                SqliteStatement stmt{m_db, "SELECT genres FROM Albums WHERE genres IS NOT NULL AND genres <> '' AND genres <> '[]';"};
                while (stmt.getNextResult())
                {
                    for (const auto &name : jsonArrayToVector(stmt.getText(0)))
                    {
                        counts[normalizeForCache(name)]++;
                    }
                }
            }

            for (auto &genre : genres)
            {
                if (const auto it = counts.find(normalizeForCache(genre.name)); it != counts.end())
                {
                    genre.albumCount = it->second;
                }
            }

            return genres;
        }

        bool SqliteAlbumManager::addGenre(const std::string& name)
        {
            const auto trimmed{trimToString(name)};
            if (trimmed.empty())
            {
                return false;
            }

            SqliteStatement stmt{m_db, "INSERT OR IGNORE INTO Genres (name) VALUES (?);"};
            stmt.addParam(trimmed);
            if (!stmt.execute())
            {
                spdlog::error("Failed to add genre '{}': {}", trimmed, m_db.getLastError());
                return false;
            }
            return true;
        }

        std::vector<std::string> SqliteAlbumManager::jsonArrayToVector(const std::string& json) const
        {
            if (json.empty())
                return {};
            
            try
            {
                auto j = nlohmann::json::parse(json);
                return j.get<std::vector<std::string>>();
            }
            catch (const std::exception& e)
            {
                spdlog::error("Failed to parse JSON array: {}", e.what());
                return {};
            }
        }

        std::string SqliteAlbumManager::vectorToJsonArray(const std::vector<std::string>& vec) const
        {
            if (vec.empty())
                return "[]";
            
            nlohmann::json j = vec;
            return j.dump();
        }

        AlbumInfo SqliteAlbumManager::albumInfoFromStatement(const SqliteStatement& stmt) const
        {
            AlbumInfo album;
            int col = 0;
            
            album.albumId = stmt.getInt64(col++);
            album.albumArtist = stmt.isNull(col) ? "" : stmt.getText(col);
            col++;
            album.title = stmt.getText(col++);
            album.year = stmt.isNull(col) ? std::nullopt : std::optional(stmt.getInt32(col));
            col++;
            album.folderId = stmt.getInt64(col++);
            
            // Parse JSON arrays
            if (!stmt.isNull(col))
                album.genres = jsonArrayToVector(stmt.getText(col));
            col++;
            
            if (!stmt.isNull(col))
                album.moods = jsonArrayToVector(stmt.getText(col));
            col++;
            
            if (!stmt.isNull(col))
                album.tags = jsonArrayToVector(stmt.getText(col));
            col++;
            
            album.bandcampUrl = stmt.isNull(col) ? "" : stmt.getText(col);
            col++;
            
            // Bitrate
            album.bitrate = stmt.isNull(col) ? std::nullopt : std::optional(stmt.getInt32(col));
            col++;
            
            // Timestamps
            album.createdAt = timestampFromInt64(stmt.getInt64(col++));
            album.updatedAt = timestampFromInt64(stmt.getInt64(col++));
            
            return album;
        }

    } // namespace database
} // namespace jucyaudio