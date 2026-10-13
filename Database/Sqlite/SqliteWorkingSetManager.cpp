#include <Database/Includes/Constants.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteStatementConstruction.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Database/Sqlite/SqliteWorkingSetManager.h>
#include <Database/TrackLibrary.h>
#include <UI/Settings.h>
#include <Utils/AssortedUtils.h>
#include <Utils/StringWriter.h>
#include <algorithm>
#include <format>
#include <nlohmann/json.hpp>
#include <random>
#include <spdlog/spdlog.h>

namespace
{
    using namespace jucyaudio;
    using namespace jucyaudio::database;

    WorkingSetInfo workingSetInfoFromStatement(const SqliteStatement &stmt)
    {
        WorkingSetInfo info{};
        int col = 0;
        info.id = stmt.getInt64(col++);
        info.name = stmt.getText(col++);
        info.timestamp = timestampFromInt64(stmt.getInt64(col++));
        if (!stmt.isNull(col))
        {
            info.sortOrder = parseSortOrderJson(stmt.getText(col));
        }
        col++;

        info.numberOfTracks = stmt.getInt64(col++);
        info.totalDuration = durationFromInt64(stmt.getInt64(col++));
        return info;
    }

    /// @brief The set's saved sort; empty if it has none. False only if the read itself failed.
    bool savedSortOrder(SqliteDatabase &db, WorkingSetId workingSetId, std::vector<SortOrderInfo> &sortOrder)
    {
        sortOrder.clear();
        SqliteStatement stmt{db, "SELECT sort_order FROM WorkingSets WHERE ws_id = ?;"};
        if (!stmt.isValid() || !stmt.addParam(workingSetId))
        {
            return false;
        }
        if (stmt.getNextResult() && !stmt.isNull(0))
        {
            sortOrder = parseSortOrderJson(stmt.getText(0));
        }
        return !stmt.hasError();
    }
} // namespace

namespace jucyaudio
{
    namespace database
    {
        std::vector<SortOrderInfo> defaultWorkingSetSortOrder()
        {
            return {SortOrderInfo{"bpm", false}};
        }

        std::vector<SortOrderInfo> parseSortOrderJson(std::string_view json)
        {
            std::vector<SortOrderInfo> sortOrder;
            if (json.empty())
            {
                return sortOrder;
            }
            try
            {
                for (const auto &item : nlohmann::json::parse(json))
                {
                    sortOrder.push_back(SortOrderInfo{item["column"].get<std::string>(), item["descending"].get<bool>()});
                }
            }
            catch (const std::exception &e)
            {
                spdlog::warn("Failed to parse working set sort_order '{}': {}", json, e.what());
                sortOrder.clear();
            }
            return sortOrder;
        }

        std::string sortOrderToJson(const std::vector<SortOrderInfo> &sortOrder)
        {
            nlohmann::json jsonArray = nlohmann::json::array();
            for (const auto &sort : sortOrder)
            {
                jsonArray.push_back({{"column", sort.columnName}, {"descending", sort.descending}});
            }
            return jsonArray.empty() ? std::string{} : jsonArray.dump();
        }

        bool numberUnpositionedTracks(SqliteDatabase &db, WorkingSetId workingSetId, const std::vector<SortOrderInfo> &sortOrder)
        {
            std::vector<std::string> trackColumns;
            {
                SqliteStatement columns{db, "PRAGMA table_info(Tracks);"};
                while (columns.getNextResult())
                {
                    trackColumns.push_back(columns.getText(1));
                }
                // An incomplete list would silently drop sort columns, and number the set by track_id alone.
                if (!columns.isValid() || columns.hasError())
                {
                    spdlog::error("numberUnpositionedTracks: could not read the columns of Tracks: {}", db.getLastError());
                    return false;
                }
            }

            // The same terms addOrderByClause writes, so a renumbered set reads back in the order the
            // column sort used to display it in.
            StringWriter orderBy;
            for (const auto &sort : sortOrder.empty() ? defaultWorkingSetSortOrder() : sortOrder)
            {
                if (std::ranges::find(trackColumns, sort.columnName) == trackColumns.end())
                {
                    spdlog::warn("numberUnpositionedTracks: working set {} sorts by '{}', which is not a column of Tracks - ignored",
                        workingSetId,
                        sort.columnName);
                    continue;
                }
                orderBy.appendFormatted("Tracks.{}{}", sort.columnName, sort.descending ? " COLLATE NOCASE DESC, " : " COLLATE NOCASE ASC, ");
            }
            orderBy.append("Tracks.track_id");

            const auto sql = std::format("UPDATE WorkingSetTracks SET position = base.m + r.rn "
                                         "FROM (SELECT COALESCE(MAX(position), 0) AS m FROM WorkingSetTracks WHERE ws_id = ?1) base, "
                                         "(SELECT Tracks.track_id, row_number() OVER (ORDER BY {}) AS rn "
                                         "FROM WorkingSetTracks w JOIN Tracks ON Tracks.track_id = w.track_id "
                                         "WHERE w.ws_id = ?1 AND w.position IS NULL) r "
                                         "WHERE WorkingSetTracks.ws_id = ?1 AND WorkingSetTracks.track_id = r.track_id;",
                orderBy.asString());
            SqliteStatement stmt{db, sql};
            if (!stmt.isValid() || !stmt.addParam(workingSetId) || !stmt.execute())
            {
                spdlog::error("numberUnpositionedTracks: working set {} failed: {}", workingSetId, db.getLastError());
                return false;
            }
            return true;
        }

        bool SqliteWorkingSetManager::insertTracksInOrder(WorkingSetId workingSetId, const std::function<bool()> &insert) const
        {
            // A row without a position is displayed after every numbered row, by track_id. Repair those
            // in exactly that order first, so nothing already on screen moves; only then does the
            // saved sort apply, and by then the only rows without a position are the new ones.
            static const std::vector<SortOrderInfo> displayedOrder{SortOrderInfo{"track_id", false}};
            std::vector<SortOrderInfo> sortOrder;
            return numberUnpositionedTracks(m_db, workingSetId, displayedOrder) && insert() && savedSortOrder(m_db, workingSetId, sortOrder) &&
                numberUnpositionedTracks(m_db, workingSetId, sortOrder);
        }

        std::vector<WorkingSetInfo> SqliteWorkingSetManager::getWorkingSets(const TrackQueryArgs &args) const
        {
            // Check if we need to filter offline working sets
            const bool filterOffline = !config::theSettings.uiSettings.showOfflineTracks;
            
            // Check if the temp table exists (only created when there are offline folders)
            bool tempTableExists = false;
            if (filterOffline)
            {
                SqliteStatement checkStmt{m_db, "SELECT name FROM sqlite_temp_master WHERE type='table' AND name='OfflineWorkingSets';"};
                tempTableExists = checkStmt.getNextResult();
            }
            
            std::string BASE_STMT;
            if (filterOffline && tempTableExists)
            {
                // Filter out offline working sets
                BASE_STMT = R"SQL(SELECT 
    ws.ws_id,
    ws.name,
    ws.timestamp,
    ws.sort_order,
    COUNT(wst.track_id) as track_count,
    SUM(t.duration) as total_duration
FROM WorkingSets ws
LEFT JOIN WorkingSetTracks wst ON ws.ws_id = wst.ws_id
LEFT JOIN Tracks t ON wst.track_id = t.track_id
WHERE ws.ws_id NOT IN (SELECT ws_id FROM temp.OfflineWorkingSets)
GROUP BY ws.ws_id, ws.name, ws.sort_order)SQL";
            }
            else
            {
                // Show all working sets
                BASE_STMT = R"SQL(SELECT 
    ws.ws_id,
    ws.name,
    ws.timestamp,
    ws.sort_order,
    COUNT(wst.track_id) as track_count,
    SUM(t.duration) as total_duration
FROM WorkingSets ws
LEFT JOIN WorkingSetTracks wst ON ws.ws_id = wst.ws_id
LEFT JOIN Tracks t ON wst.track_id = t.track_id
GROUP BY ws.ws_id, ws.name, ws.sort_order)SQL";
            }

            StringWriter output;
            output.append(BASE_STMT);
            if (!args.searchTerms.empty())
            {
                // Check if we already have a WHERE clause (from offline filtering)
                if (filterOffline && tempTableExists)
                {
                    output.append(" AND ");
                }
                else
                {
                    output.append(" WHERE ");
                }
                bool first = true;
                for (const auto &searchTerm : args.searchTerms)
                {
                    if (first)
                    {
                        first = false;
                    }
                    else
                    {
                        output.append(" AND ");
                    }
                    output.append("ws.name LIKE '%");
                    output.append(searchTerm);
                    output.append("%'");
                }
            }
            if (!args.sortBy.empty())
            {
                output.append(" ORDER BY ");
                bool first = true;
                for (const auto &sortOrder : args.sortBy)
                {
                    if (first)
                    {
                        first = false;
                    }
                    else
                    {
                        output.append(", ");
                    }
                    output.append(sortOrder.columnName);
                    if (sortOrder.descending)
                        output.append(" DESC");
                    else
                        output.append(" ASC");
                }
            }
            const auto sql_statement = output.asString();
            spdlog::debug("Executing SQL statement to get mixes: {}", sql_statement);
            std::vector<WorkingSetInfo> workingSets;
            SqliteStatement stmt{m_db};
            stmt.query(
                [&workingSets, &stmt]() -> bool
                {
                    workingSets.emplace_back(workingSetInfoFromStatement(stmt));
                    return true;
                },
                sql_statement);
            return workingSets;
        }

        bool SqliteWorkingSetManager::createWorkingSetFromQuery(const TrackQueryArgs &args, std::string_view name, WorkingSetInfo &newWorkingSet) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                // todo: name-uniqueness should be checked by SQL
                newWorkingSet.name = name; // Set the name in the output parameter
                newWorkingSet.timestamp = std::chrono::system_clock::now();
                newWorkingSet.id = 0;
                newWorkingSet.sortOrder = defaultWorkingSetSortOrder();

                if (transaction.execute("INSERT INTO WorkingSets (name, timestamp, sort_order) VALUES (?, ?, ?);",
                        name,
                        timestampToInt64(newWorkingSet.timestamp),
                        sortOrderToJson(newWorkingSet.sortOrder)))
                {
                    newWorkingSet.id = m_db.getLastInsertRowId(); // Get the new working set ID
                    SqliteStatement stmt{m_db};
                    SqliteStatementConstruction stmtConstruction{stmt};
                    const auto inserted = stmtConstruction.createInsertIntoSelectTrackIdsStatement(args, newWorkingSet.id) &&
                        insertTracksInOrder(newWorkingSet.id,
                            [&stmt]
                            {
                                return stmt.execute();
                            });
                    // A failure here would otherwise commit an empty set and report it as created (#74).
                    return inserted ? transaction.commit() : transaction.rollback();
                }
            }
            return false;
        }

        bool SqliteWorkingSetManager::createWorkingSetFromVirtualFolder(
            int64_t folderId, std::string_view name, WorkingSetInfo &newWorkingSet, bool recursive) const
        {
            const auto &folderDb = theTrackLibrary.getFolderDatabase();
            const auto allChildren = folderDb.getAllChildFolders({folderId});

            if (SqliteTransaction transaction{m_db})
            {
                // todo: name-uniqueness should be checked by SQL
                newWorkingSet.name = name;
                newWorkingSet.timestamp = std::chrono::system_clock::now();
                newWorkingSet.id = 0;
                newWorkingSet.sortOrder = defaultWorkingSetSortOrder();

                // create the workingset itself
                if (transaction.execute("INSERT INTO WorkingSets (name, timestamp, sort_order) VALUES (?, ?, ?);",
                        name,
                        timestampToInt64(newWorkingSet.timestamp),
                        sortOrderToJson(newWorkingSet.sortOrder)))
                {
                    newWorkingSet.id = m_db.getLastInsertRowId(); // Get the new working set ID

                    // Build recursive CTE to get all tracks in folder and subfolders
                    if (recursive)
                    {
                        StringWriter stmt;
                        stmt.append("INSERT INTO WorkingSetTracks (ws_id, track_id) ");
                        stmt.append("SELECT ?, track_id FROM Tracks WHERE folder_id IN (");
                        bool first = true;
                        for (const auto& childFolderId : allChildren)
                        {
                            if (first)
                            {
                                first = false;
                            }
                            else
                            {
                                stmt.append(", ");
                            }
                            stmt.append(std::to_string(childFolderId));
                        }
                        stmt.append(");");
                        if (insertTracksInOrder(newWorkingSet.id,
                                [&]
                                {
                                    return transaction.execute(stmt.asString(), newWorkingSet.id);
                                }))
                        {
                            return transaction.commit();
                        }
                    }
                    else
                    {
                        // Non-recursive: only tracks in this specific folder
                        if (insertTracksInOrder(newWorkingSet.id,
                                [&]
                                {
                                    return transaction.execute("INSERT INTO WorkingSetTracks (ws_id, track_id) "
                                                               "SELECT ?, track_id FROM Tracks WHERE folder_id = ?;",
                                        newWorkingSet.id,
                                        folderId);
                                }))
                        {
                            return transaction.commit();
                        }
                    }
                }
            }
            return false;
        }

        bool SqliteWorkingSetManager::createWorkingSetFromTrackInfos(
            const std::vector<TrackInfo> &trackInfos, std::string_view name, WorkingSetInfo &newWorkingSet) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                // todo: name-uniqueness should be checked by SQL
                newWorkingSet.name = name;
                newWorkingSet.timestamp = std::chrono::system_clock::now();
                newWorkingSet.id = 0;
                newWorkingSet.sortOrder = defaultWorkingSetSortOrder();

                if (transaction.execute("INSERT INTO WorkingSets (name, timestamp, sort_order) VALUES (?, ?, ?);",
                        name,
                        timestampToInt64(newWorkingSet.timestamp),
                        sortOrderToJson(newWorkingSet.sortOrder)))
                {
                    newWorkingSet.id = m_db.getLastInsertRowId(); // Get the new working set ID
                    const auto inserted = insertTracksInOrder(newWorkingSet.id,
                        [&]
                        {
                            for (const auto &trackInfo : trackInfos)
                            {
                                if (!transaction.execute("INSERT OR IGNORE INTO "
                                                         "WorkingSetTracks (ws_id, "
                                                         "track_id) VALUES (?, ?);",
                                        newWorkingSet.id,
                                        trackInfo.trackId))
                                {
                                    return false;
                                }
                            }
                            return true;
                        });
                    return inserted ? transaction.commit() : transaction.rollback();
                }
            }
            return false;
        }

        bool SqliteWorkingSetManager::addToWorkingSet(WorkingSetId workingSetId, const std::vector<TrackInfo> &trackInfos)
        {
            if (SqliteTransaction transaction{m_db})
            {
                const auto inserted = insertTracksInOrder(workingSetId,
                    [&]
                    {
                        for (const auto &trackInfo : trackInfos)
                        {
                            if (!transaction.execute("INSERT OR IGNORE INTO "
                                                     "WorkingSetTracks (ws_id, "
                                                     "track_id) VALUES (?, ?);",
                                    workingSetId,
                                    trackInfo.trackId))
                            {
                                return false;
                            }
                        }
                        return true;
                    });
                return inserted ? transaction.commit() : transaction.rollback();
            }
            return false;
        }

        bool SqliteWorkingSetManager::addVirtualFolderToWorkingSet(WorkingSetId workingSetId, int64_t folderId, bool recursive)
        {
            const auto &folderDb = theTrackLibrary.getFolderDatabase();
            const auto allChildren = folderDb.getAllChildFolders({folderId});

            if (SqliteTransaction transaction{m_db})
            {
                if (recursive)
                {
                    StringWriter stmt;
                    stmt.append("INSERT OR IGNORE INTO WorkingSetTracks (ws_id, track_id) ");
                    stmt.append("SELECT ?, track_id FROM Tracks WHERE folder_id IN (");
                    bool first = true;
                    for (const auto &childFolderId : allChildren)
                    {
                        if (first)
                        {
                            first = false;
                        }
                        else
                        {
                            stmt.append(", ");
                        }
                        stmt.append(std::to_string(childFolderId));
                    }
                    stmt.append(");");
                    return insertTracksInOrder(workingSetId,
                               [&]
                               {
                                   return transaction.execute(stmt.asString(), workingSetId);
                               }) &&
                        transaction.commit();
                }

                return insertTracksInOrder(workingSetId,
                           [&]
                           {
                               return transaction.execute("INSERT OR IGNORE INTO WorkingSetTracks (ws_id, track_id) "
                                                          "SELECT ?, track_id FROM Tracks WHERE folder_id = ?;",
                                   workingSetId,
                                   folderId);
                           }) &&
                    transaction.commit();
            }

            return false;
        }

        bool SqliteWorkingSetManager::renameWorkingSet(WorkingSetId workingSetId, std::string_view name) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                std::ignore = transaction.execute("UPDATE WorkingSets SET name=? WHERE ws_id=?;", name, workingSetId);
                return transaction.commit();
            }
            return false;
        }

        bool SqliteWorkingSetManager::removeTracksFromWorkingSet(WorkingSetId workingSetId, const std::vector<TrackId> &trackIds)
        {
            assert(!trackIds.empty());
            if (SqliteTransaction transaction{m_db})
            {
                for (const auto &trackId : trackIds)
                {
                    if (!transaction.execute("DELETE FROM WorkingSetTracks "
                                             "WHERE ws_id = ? AND "
                                             "track_id = ?;",
                            workingSetId,
                            trackId))
                    {
                        return transaction.rollback();
                    }
                }
                return transaction.commit();
            }
            return false;
        }

        bool SqliteWorkingSetManager::removeTrackFromWorkingSet(WorkingSetId workingSetId, TrackId trackId)
        {
            if (SqliteTransaction transaction{m_db})
            {
                if (transaction.execute("DELETE FROM WorkingSetTracks WHERE "
                                        "ws_id = ? AND track_id = ?;",
                        workingSetId,
                        trackId))
                {
                    return transaction.commit();
                }
            }
            return false;
        }

        bool SqliteWorkingSetManager::removeWorkingSet(WorkingSetId workingSetId)
        {
            if (SqliteTransaction transaction{m_db})
            {
                if (transaction.execute("DELETE FROM WorkingSetTracks WHERE ws_id = ?;", workingSetId))
                {
                    if (transaction.execute("DELETE FROM WorkingSets WHERE ws_id = ?;", workingSetId))
                    {
                        return transaction.commit();
                    }
                }
            }
            return false;
        }

        bool SqliteWorkingSetManager::removeWorkingSets(const std::vector<WorkingSetId> &workingSetIds)
        {
            if (SqliteTransaction transaction{m_db})
            {
                for (const auto workingSetId : workingSetIds)
                {
                    if (!transaction.execute("DELETE FROM WorkingSetTracks WHERE ws_id = ?;", workingSetId))
                    {
                        return transaction.rollback();
                    }
                    if (!transaction.execute("DELETE FROM WorkingSets WHERE ws_id = ?;", workingSetId))
                    {
                        return transaction.rollback();
                    }
                }
                return transaction.commit();
            }
            return false;
        }

        bool SqliteWorkingSetManager::applySortOrder(WorkingSetId workingSetId, const std::vector<SortOrderInfo> &sortOrder)
        {
            if (SqliteTransaction transaction{m_db})
            {
                // The sort is saved for the header arrow; the positions are what the set is displayed by.
                const auto sortOrderJson = sortOrderToJson(sortOrder);
                const bool sorted = transaction.execute("UPDATE WorkingSetTracks SET position = NULL WHERE ws_id = ?;", workingSetId) &&
                    numberUnpositionedTracks(m_db, workingSetId, sortOrder) &&
                    (sortOrderJson.empty() ? transaction.execute("UPDATE WorkingSets SET sort_order = NULL WHERE ws_id = ?;", workingSetId)
                                           : transaction.execute("UPDATE WorkingSets SET sort_order = ? WHERE ws_id = ?;", sortOrderJson, workingSetId));
                if (!sorted)
                {
                    spdlog::error("Failed to apply sort order to working set {}: {}", workingSetId, m_db.getLastError());
                    return transaction.rollback();
                }
                return transaction.commit();
            }
            return false;
        }

        bool SqliteWorkingSetManager::shuffleTracks(WorkingSetId workingSetId, const std::vector<TrackId> &trackIds)
        {
            if (trackIds.size() < 2)
            {
                return true;
            }

            if (SqliteTransaction transaction{m_db})
            {
                // A row without a position has none to give up, so number those first - in track_id
                // order, which is where they are displayed, so the repair moves nothing on screen.
                static const std::vector<SortOrderInfo> displayedOrder{SortOrderInfo{"track_id", false}};
                if (!numberUnpositionedTracks(m_db, workingSetId, displayedOrder))
                {
                    return transaction.rollback();
                }

                StringWriter select;
                select.append("SELECT track_id, position FROM WorkingSetTracks WHERE ws_id = ? AND track_id IN (");
                for (size_t i = 0; i < trackIds.size(); ++i)
                {
                    select.append(i == 0 ? "" : ",");
                    select.append(std::to_string(trackIds[i]));
                }
                select.append(");");

                std::vector<TrackId> tracks;
                std::vector<int64_t> positions;
                {
                    SqliteStatement stmt{m_db, select.asString()};
                    if (!stmt.isValid() || !stmt.addParam(workingSetId))
                    {
                        spdlog::error("Failed to read the positions to shuffle in working set {}: {}", workingSetId, m_db.getLastError());
                        return transaction.rollback();
                    }
                    while (stmt.getNextResult())
                    {
                        tracks.push_back(stmt.getInt64(0));
                        positions.push_back(stmt.getInt64(1));
                    }
                    // A read that stopped part way would hand out only some of the positions: the
                    // tracks after the failure would keep theirs, and the shuffle would report success.
                    if (stmt.hasError())
                    {
                        spdlog::error("Reading the positions to shuffle in working set {} failed part way: {}", workingSetId, m_db.getLastError());
                        return transaction.rollback();
                    }
                }

                // Only the positions these tracks already hold are handed out again, so every other
                // track keeps its row, and the selected rows still hold exactly the selected tracks.
                std::mt19937 generator{std::random_device{}()};
                std::ranges::shuffle(positions, generator);

                SqliteStatement update{m_db, "UPDATE WorkingSetTracks SET position = ? WHERE ws_id = ? AND track_id = ?;"};
                for (size_t i = 0; i < tracks.size(); ++i)
                {
                    update.reset();
                    if (!update.addParam(positions[i]) || !update.addParam(workingSetId) || !update.addParam(tracks[i]) || !update.execute())
                    {
                        spdlog::error("Failed to shuffle working set {}: {}", workingSetId, m_db.getLastError());
                        return transaction.rollback();
                    }
                }
                spdlog::info("Shuffled {} track(s) in working set {}", tracks.size(), workingSetId);
                return transaction.commit();
            }
            return false;
        }

        int SqliteWorkingSetManager::getNextMixNumber(WorkingSetId workingSetId) const
        {
            SqliteStatement stmt{m_db, "SELECT next_mix_number FROM WorkingSets WHERE ws_id = ?;"};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare getNextMixNumber statement: {}", m_db.getLastError());
                return 0;
            }

            stmt.addParam(workingSetId);
            if (!stmt.getNextResult())
            {
                spdlog::error("Failed to get next_mix_number for working set {}: {}", workingSetId, m_db.getLastError());
                return 0;
            }

            int mixNumber = stmt.getInt32(0);
            if (mixNumber == 0)
            {
                mixNumber = 1; // Default if null/zero
            }

            return mixNumber;
        }

        bool SqliteWorkingSetManager::incrementMixNumber(WorkingSetId workingSetId)
        {
            // Get current value
            int currentNumber = getNextMixNumber(workingSetId);
            if (currentNumber == 0)
            {
                return false;
            }

            // Increment it
            SqliteStatement stmt{m_db, "UPDATE WorkingSets SET next_mix_number = ? WHERE ws_id = ?;"};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare incrementMixNumber statement: {}", m_db.getLastError());
                return false;
            }

            stmt.addParam(currentNumber + 1);
            stmt.addParam(workingSetId);

            if (!stmt.execute())
            {
                spdlog::error("Failed to increment mix number for working set {}: {}", workingSetId, m_db.getLastError());
                return false;
            }

            return true;
        }

        bool SqliteWorkingSetManager::setNextMixNumber(WorkingSetId workingSetId, int nextMixNumber)
        {
            if (nextMixNumber < 1)
            {
                spdlog::error("Invalid mix number value: {}. Must be >= 1.", nextMixNumber);
                return false;
            }

            SqliteStatement stmt{m_db, "UPDATE WorkingSets SET next_mix_number = ? WHERE ws_id = ?;"};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare setNextMixNumber statement: {}", m_db.getLastError());
                return false;
            }

            stmt.addParam(nextMixNumber);
            stmt.addParam(workingSetId);

            if (!stmt.execute())
            {
                spdlog::error("Failed to set mix number for working set {}: {}", workingSetId, m_db.getLastError());
                return false;
            }

            spdlog::info("Set next mix number to {} for working set {}", nextMixNumber, workingSetId);
            return true;
        }

    } // namespace database
} // namespace jucyaudio
