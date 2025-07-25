#include <Database/Includes/Constants.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteStatementConstruction.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Database/Sqlite/SqliteWorkingSetManager.h>
#include <Utils/AssortedUtils.h>
#include <nlohmann/json.hpp>
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

        // Parse sort_order JSON if present
        if (!stmt.isNull(col))
        {
            const std::string sortOrderJson = stmt.getText(col);
            if (!sortOrderJson.empty())
            {
                try
                {
                    auto json = nlohmann::json::parse(sortOrderJson);
                    for (const auto &item : json)
                    {
                        SortOrderInfo sortInfo;
                        sortInfo.columnName = item["column"].get<std::string>();
                        sortInfo.descending = item["descending"].get<bool>();
                        info.sortOrder.push_back(sortInfo);
                    }
                }
                catch (const std::exception &e)
                {
                    spdlog::warn("Failed to parse sort_order JSON for working set {}: {}", info.id, e.what());
                }
            }
        }
        col++;

        info.track_count = stmt.getInt64(col++);
        info.total_duration = durationFromInt64(stmt.getInt64(col++));
        return info;
    }
} // namespace

namespace jucyaudio
{
    namespace database
    {

        std::vector<WorkingSetInfo> SqliteWorkingSetManager::getWorkingSets(const TrackQueryArgs &args) const
        {
            const std::string BASE_STMT = R"SQL(SELECT 
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

            StringWriter output;
            output.append(BASE_STMT);
            if (!args.searchTerms.empty())
            {
                output.append(" WHERE ");
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
                    output.append("m.name LIKE '%");
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

                if (transaction.execute("INSERT INTO WorkingSets (name, "
                                        "timestamp) VALUES (?, ?);",
                        name,
                        timestampToInt64(newWorkingSet.timestamp)))
                {
                    newWorkingSet.id = m_db.getLastInsertRowId(); // Get the new working set ID
                    SqliteStatement stmt{m_db};
                    SqliteStatementConstruction stmtConstruction{stmt};
                    if (stmtConstruction.createInsertIntoSelectTrackIdsStatement(args, newWorkingSet.id))
                    {
                        if (stmt.execute())
                        {
                            return transaction.commit();
                        }
                    }
                    return transaction.commit();
                }
            }
            return false;
        }

        bool SqliteWorkingSetManager::createWorkingSetFromVirtualFolder(
            int64_t folderId, std::string_view name, WorkingSetInfo &newWorkingSet, bool recursive) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                // todo: name-uniqueness should be checked by SQL
                newWorkingSet.name = name;
                newWorkingSet.timestamp = std::chrono::system_clock::now();
                newWorkingSet.id = 0;

                if (transaction.execute("INSERT INTO WorkingSets (name, "
                                        "timestamp) VALUES (?, ?);",
                        name,
                        timestampToInt64(newWorkingSet.timestamp)))
                {
                    newWorkingSet.id = m_db.getLastInsertRowId(); // Get the new working set ID

                    // Build recursive CTE to get all tracks in folder and subfolders
                    if (recursive)
                    {
                        const char *recursiveQuery = R"(
                            WITH RECURSIVE folder_tree AS (
                                SELECT folder_id FROM VirtualFolders WHERE folder_id = ?
                                UNION ALL
                                SELECT vf.folder_id 
                                FROM VirtualFolders vf
                                INNER JOIN folder_tree ft ON vf.parent_id = ft.folder_id
                            )
                            INSERT INTO WorkingSetTracks (ws_id, track_id)
                            SELECT ?, track_id 
                            FROM Tracks 
                            WHERE virtual_folder_id IN (SELECT folder_id FROM folder_tree);
                        )";

                        if (transaction.execute(recursiveQuery, folderId, newWorkingSet.id))
                        {
                            return transaction.commit();
                        }
                    }
                    else
                    {
                        // Non-recursive: only tracks in this specific folder
                        if (transaction.execute("INSERT INTO WorkingSetTracks (ws_id, track_id) "
                                                "SELECT ?, track_id FROM Tracks WHERE virtual_folder_id = ?;",
                                newWorkingSet.id,
                                folderId))
                        {
                            return transaction.commit();
                        }
                    }
                }
            }
            return false;
        }

        bool SqliteWorkingSetManager::createWorkingSetFromTrackIds(
            const std::vector<TrackId> &trackIds, std::string_view name, WorkingSetInfo &newWorkingSet) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                // todo: name-uniqueness should be checked by SQL
                newWorkingSet.name = name;
                newWorkingSet.timestamp = std::chrono::system_clock::now();
                newWorkingSet.id = 0;

                if (transaction.execute("INSERT INTO WorkingSets (name, "
                                        "timestamp) VALUES (?, ?);",
                        name,
                        timestampToInt64(newWorkingSet.timestamp)))
                {
                    newWorkingSet.id = m_db.getLastInsertRowId(); // Get the new working set ID
                    for (const auto &trackId : trackIds)
                    {
                        if (!transaction.execute("INSERT OR IGNORE INTO "
                                                 "WorkingSetTracks (ws_id, "
                                                 "track_id) VALUES (?, ?);",
                                newWorkingSet.id,
                                trackId))
                        {
                            return transaction.rollback();
                        }
                    }
                    return transaction.commit();
                }
            }
            return false;
        }

        bool SqliteWorkingSetManager::addToWorkingSet(WorkingSetId workingSetId, const std::vector<TrackId> &trackIds)
        {
            if (SqliteTransaction transaction{m_db})
            {
                for (const auto &trackId : trackIds)
                {
                    if (!transaction.execute("INSERT OR IGNORE INTO "
                                             "WorkingSetTracks (ws_id, "
                                             "track_id) VALUES (?, ?);",
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

        bool SqliteWorkingSetManager::addToWorkingSet(WorkingSetId workingSetId, TrackId trackId)
        {
            if (SqliteTransaction transaction{m_db})
            {
                if (transaction.execute("INSERT OR IGNORE INTO WorkingSetTracks (ws_id, "
                                        "track_id) VALUES (?, ?);",
                        workingSetId,
                        trackId))
                {
                    return transaction.commit();
                }
                return transaction.commit();
            }
            return false;
        }

        bool SqliteWorkingSetManager::renameWorkingSet(WorkingSetId workingSetId, std::string_view name) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                if (transaction.execute("UPDATE WorkingSets SET name=? WHERE ws_id=?;", name, workingSetId))
                {
                    return transaction.commit();
                }
                return transaction.commit();
            }
            return false;
        }

        bool SqliteWorkingSetManager::removeFromWorkingSet(WorkingSetId workingSetId, const std::vector<TrackId> &trackIds)
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

        bool SqliteWorkingSetManager::removeFromWorkingSet(WorkingSetId workingSetId, TrackId trackId)
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

        bool SqliteWorkingSetManager::removeWorkingSets(const std::vector<WorkingSetId>& workingSetIds)
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

        bool SqliteWorkingSetManager::updateSortOrder(WorkingSetId workingSetId, const std::vector<SortOrderInfo> &sortOrder)
        {
            // Convert sort order to JSON
            nlohmann::json jsonArray = nlohmann::json::array();
            for (const auto &sort : sortOrder)
            {
                nlohmann::json sortObj;
                sortObj["column"] = sort.columnName;
                sortObj["descending"] = sort.descending;
                jsonArray.push_back(sortObj);
            }

            const std::string sortOrderJson = jsonArray.empty() ? "" : jsonArray.dump();

            SqliteStatement stmt{m_db, "UPDATE WorkingSets SET sort_order = ? WHERE ws_id = ?;"};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare updateSortOrder statement: {}", m_db.getLastError());
                return false;
            }

            // Bind parameters - use null for empty sort order
            if (sortOrderJson.empty())
            {
                stmt.addNullParam();
            }
            else
            {
                stmt.addParam(sortOrderJson);
            }
            stmt.addParam(workingSetId);

            if (!stmt.execute())
            {
                spdlog::error("Failed to update sort order for working set {}: {}", workingSetId, m_db.getLastError());
                return false;
            }

            return true;
        }

    } // namespace database
} // namespace jucyaudio
