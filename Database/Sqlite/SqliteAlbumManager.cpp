#include <Database/Sqlite/SqliteAlbumManager.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Database/TrackLibrary.h>
#include <UI/Settings.h>
#include <Utils/AssortedUtils.h>
#include <nlohmann/json.hpp>
#include <algorithm>
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
            if (!transaction)
            {
                // The statements below go straight to the connection, so nothing else would stop them
                // from running - and committing one at a time - outside a transaction that never began.
                return false;
            }
            
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
                        // Keyed the way the Genres table identifies its rows, not the way the
                        // filesystem cache identifies paths. Two names that SQLite keeps as two
                        // rows must be counted as two entries, or the cloud shows one chip
                        // carrying both rows' albums and renaming it moves the wrong ones.
                        counts[noCaseKey(name)]++;
                    }
                }
            }

            for (auto &genre : genres)
            {
                if (const auto it = counts.find(noCaseKey(genre.name)); it != counts.end())
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

        bool SqliteAlbumManager::renameGenre(const std::string& oldName, const std::string& newName, bool* mergedOut)
        {
            const auto from{trimToString(oldName)};
            const auto to{trimToString(newName)};
            if (from.empty() || to.empty())
            {
                spdlog::error("Refusing to rename genre '{}' to '{}': neither may be empty.", from, to);
                return false;
            }

            // noCaseKey throughout, so that matching an album label against a vocabulary entry uses
            // the same notion of sameness SQLite used to resolve the rows above.
            const auto fromKey{noCaseKey(from)};

            // One transaction around the read of Albums below and the writes that follow, so they see
            // one state of the database: this rewrites whole JSON arrays, and a relabel landing in
            // between would be a lost update that silently drops an album's other genres, not just the
            // one being renamed.
            SqliteTransaction transaction{m_db};
            if (!transaction)
            {
                spdlog::error("Could not begin a transaction to rename genre '{}'.", from);
                return false;
            }

            // Both rows are resolved here, inside the transaction, rather than assumed from what the
            // caller believed when it opened its dialog.
            //
            // The source has to exist. Without this check a stale or misspelled oldName would fall
            // through to the insert-and-delete below, adding a new vocabulary entry and deleting
            // nothing - a rename of something that was not there, which is an invention.
            int64_t fromId = 0;
            std::string storedFrom;
            {
                SqliteStatement stmt{m_db, "SELECT genre_id, name FROM Genres WHERE name = ? COLLATE NOCASE;"};
                stmt.addParam(from);
                if (!stmt.getNextResult())
                {
                    spdlog::error("Refusing to rename genre '{}': it is not in the vocabulary.", from);
                    return false;
                }
                fromId = stmt.getInt64(0);
                storedFrom = stmt.getText(1);
            }

            // The target's row, not merely whether one exists. Its stored spelling matters because
            // COLLATE NOCASE means renaming "drone" to "Ambient" finds a row spelled "ambient" - and
            // labelling the albums "Ambient" would give them a spelling the vocabulary does not
            // offer. On a merge the surviving row wins.
            int64_t toId = 0;
            std::string storedTo;
            {
                SqliteStatement stmt{m_db, "SELECT genre_id, name FROM Genres WHERE name = ? COLLATE NOCASE;"};
                stmt.addParam(to);
                if (stmt.getNextResult())
                {
                    toId = stmt.getInt64(0);
                    storedTo = stmt.getText(1);
                }
            }

            // Row identity, not a comparison of the names at all. Whether the target resolves to the
            // row we started from is the only question that matters, and SQLite has already answered
            // it by matching, or not matching, this row - so no comparator of ours can disagree with
            // the table about which rows exist.
            const bool recasingOnly = (toId == fromId);
            const bool merging = (toId != 0) && !recasingOnly;

            if (recasingOnly && storedFrom == to)
            {
                // The stored spelling is already the one asked for. Not an error: a dialog
                // prefilled with the current name and dismissed with Rename should do nothing,
                // rather than report a failure the user has to read. Checked here rather than up
                // front so that a name the vocabulary does not hold is still refused.
                if (mergedOut != nullptr)
                {
                    *mergedOut = false;
                }
                return true;
            }

            // What the albums actually get labelled with. A merge adopts the surviving row's
            // spelling; a rename or a recase writes what was asked for.
            const auto effectiveTo{merging ? storedTo : to};
            const auto effectiveToKey{noCaseKey(effectiveTo)};

            // Only labelled albums are read - a few hundred rows out of the whole table - and only
            // the ones actually carrying the old name are written back.
            std::vector<std::pair<AlbumId, std::vector<std::string>>> relabelled;
            {
                SqliteStatement stmt{m_db, "SELECT album_id, genres FROM Albums WHERE genres IS NOT NULL AND genres <> '' AND genres <> '[]';"};
                while (stmt.getNextResult())
                {
                    const auto albumId{static_cast<AlbumId>(stmt.getInt64(0))};
                    const auto current{jsonArrayToVector(stmt.getText(1))};

                    // Albums that never carried the old name are left alone, full stop. Without
                    // this an album listing the merge target twice would be tidied up on the way
                    // past, so renaming one genre would rewrite rows that do not use it.
                    const auto carriesSource{std::any_of(current.begin(),
                        current.end(),
                        [&fromKey](const std::string& entry) { return noCaseKey(entry) == fromKey; })};
                    if (!carriesSource)
                    {
                        continue;
                    }

                    std::vector<std::string> next;
                    next.reserve(current.size());
                    bool changed = false;
                    for (const auto& entry : current)
                    {
                        const auto replacement{noCaseKey(entry) == fromKey ? effectiveTo : entry};
                        changed = changed || (replacement != entry);

                        // Order is preserved and duplicates dropped: the first element is the
                        // headline genre and the rest are secondary, so an album that carried both
                        // names must end up with one entry, in the position the earlier of the two
                        // held. Without this a merge would leave the same genre listed twice.
                        //
                        // Only a duplicate of the name being merged into counts. An album may already
                        // list some unrelated genre twice, and tidying that up here would be an edit
                        // nobody asked for.
                        const auto replacementKey{noCaseKey(replacement)};
                        const auto alreadyPresent{replacementKey == effectiveToKey && std::any_of(next.begin(),
                            next.end(),
                            [&replacementKey](const std::string& seen) { return noCaseKey(seen) == replacementKey; })};
                        if (alreadyPresent)
                        {
                            changed = true;
                            continue;
                        }
                        next.push_back(replacement);
                    }

                    if (changed)
                    {
                        relabelled.emplace_back(albumId, std::move(next));
                    }
                }
            }

            for (const auto& [albumId, genres] : relabelled)
            {
                // Only the genres column, deliberately. updateAlbumMetadata rewrites moods and tags
                // as well, and this function is not given them - going through it would blank both.
                if (!transaction.execute("UPDATE Albums SET genres = ?, updated_at = CURRENT_TIMESTAMP WHERE album_id = ?;",
                        vectorToJsonArray(genres),
                        albumId))
                {
                    spdlog::error("Failed to relabel album {} while renaming genre '{}': {}", albumId, from, m_db.getLastError());
                    return false;
                }
            }

            if (recasingOnly)
            {
                // The two names are the same row under the unique index, so this is the one case an
                // UPDATE handles and an insert-then-delete would not: inserting matches the existing
                // row and does nothing, and deleting by name would then remove the row being kept.
                if (!transaction.execute("UPDATE Genres SET name = ? WHERE genre_id = ?;", to, fromId))
                {
                    spdlog::error("Failed to recase genre '{}' to '{}': {}", from, to, m_db.getLastError());
                    return false;
                }
            }
            else
            {
                // INSERT OR IGNORE then DELETE serves both remaining cases with one path: when the
                // target is new the insert creates it, when it already exists the insert does nothing
                // and this is the merge, and either way the old row goes. A plain UPDATE would
                // violate the unique index in the merge case.
                if (!transaction.execute("INSERT OR IGNORE INTO Genres (name) VALUES (?);", to))
                {
                    spdlog::error("Failed to add genre '{}' while renaming: {}", to, m_db.getLastError());
                    return false;
                }

                if (!transaction.execute("DELETE FROM Genres WHERE genre_id = ?;", fromId))
                {
                    spdlog::error("Failed to remove genre '{}' while renaming: {}", from, m_db.getLastError());
                    return false;
                }
            }

            if (!transaction.commit())
            {
                spdlog::error("Failed to commit the rename of genre '{}' to '{}'.", from, to);
                return false;
            }

            if (mergedOut != nullptr)
            {
                *mergedOut = merging;
            }

            spdlog::info("Renamed genre '{}' to '{}'{}; {} album(s) relabelled.",
                storedFrom,
                effectiveTo,
                merging ? " (merged)" : "",
                relabelled.size());
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