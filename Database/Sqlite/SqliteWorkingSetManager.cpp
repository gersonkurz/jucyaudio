#include <Database/Includes/Constants.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteStatementConstruction.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Database/Sqlite/SqliteWorkingSetManager.h>
#include <Utils/AssortedUtils.h>
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
        info.track_count = stmt.getInt64(col++);
        info.total_duration = durationFromInt64(stmt.getInt64(col++));
        return info;
    }
} // namespace

namespace jucyaudio
{
    namespace database
    {

        std::vector<WorkingSetInfo> SqliteWorkingSetManager::getWorkingSets(
            const TrackQueryArgs &args) const
        {
            const std::string BASE_STMT = R"SQL(SELECT 
    ws.ws_id,
    ws.name,
    ws.timestamp,
    COUNT(wst.track_id) as track_count,
    SUM(t.duration) as total_duration
FROM WorkingSets ws
LEFT JOIN WorkingSetTracks wst ON ws.ws_id = wst.ws_id
LEFT JOIN Tracks t ON wst.track_id = t.track_id
GROUP BY ws.ws_id, ws.name)SQL";

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
            const auto sql_statement = output.as_string();
            spdlog::debug("Executing SQL statement to get mixes: {}",
                         sql_statement);
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

        bool SqliteWorkingSetManager::createWorkingSetFromQuery(
            const TrackQueryArgs &args, std::string_view name,
            WorkingSetInfo &newWorkingSet) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                // todo: name-uniqueness should be checked by SQL
                newWorkingSet.name =
                    name; // Set the name in the output parameter
                newWorkingSet.timestamp = std::chrono::system_clock::now();
                newWorkingSet.id = 0;

                if (transaction.execute("INSERT INTO WorkingSets (name, "
                                        "timestamp) VALUES (?, ?);",
                        name, timestampToInt64(newWorkingSet.timestamp)))
                {
                    newWorkingSet.id =
                        m_db.getLastInsertRowId(); // Get the new working set ID
                    SqliteStatement stmt{m_db};
                    SqliteStatementConstruction stmtConstruction{stmt};
                    if (stmtConstruction
                            .createInsertIntoSelectTrackIdsStatement(
                                args, newWorkingSet.id))
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

        bool SqliteWorkingSetManager::createWorkingSetFromTrackIds(
            const std::vector<TrackId> &trackIds, std::string_view name,
            WorkingSetInfo &newWorkingSet) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                // todo: name-uniqueness should be checked by SQL
                newWorkingSet.name = name;
                newWorkingSet.timestamp = std::chrono::system_clock::now();
                newWorkingSet.id = 0;

                if (transaction.execute("INSERT INTO WorkingSets (name, "
                                        "timestamp) VALUES (?, ?);",
                        name, timestampToInt64(newWorkingSet.timestamp)))
                {
                    newWorkingSet.id =
                        m_db.getLastInsertRowId(); // Get the new working set ID
                    for (const auto &trackId : trackIds)
                    {
                        if (!transaction.execute("INSERT OR IGNORE INTO "
                                                 "WorkingSetTracks (ws_id, "
                                                 "track_id) VALUES (?, ?);",
                                                 newWorkingSet.id, trackId))
                        {
                            return transaction.rollback();
                        }
                    }
                    return transaction.commit();
                }
            }
            return false;
        }

        bool SqliteWorkingSetManager::addToWorkingSet(
            WorkingSetId workingSetId, const std::vector<TrackId> &trackIds)
        {
            if (SqliteTransaction transaction{m_db})
            {
                for (const auto &trackId : trackIds)
                {
                    if (!transaction.execute("INSERT OR IGNORE INTO "
                                             "WorkingSetTracks (ws_id, "
                                             "track_id) VALUES (?, ?);",
                                             workingSetId, trackId))
                    {
                        return transaction.rollback();
                    }
                }
                return transaction.commit();
            }
            return false;
        }

        bool SqliteWorkingSetManager::addToWorkingSet(WorkingSetId workingSetId,
                                                      TrackId trackId)
        {
            if (SqliteTransaction transaction{m_db})
            {
                if (transaction.execute(
                        "INSERT OR IGNORE INTO WorkingSetTracks (ws_id, "
                        "track_id) VALUES (?, ?);",
                        workingSetId, trackId))
                {
                    return transaction.commit();
                }
                return transaction.commit();
            }
            return false;
        }

        bool SqliteWorkingSetManager::removeFromWorkingSet(
            WorkingSetId workingSetId, const std::vector<TrackId> &trackIds)
        {
            assert(!trackIds.empty());
            if (SqliteTransaction transaction{m_db})
            {
                for (const auto &trackId : trackIds)
                {
                    if (!transaction.execute("DELETE FROM WorkingSetTracks "
                                             "WHERE ws_id = ? AND "
                                             "track_id = ?;",
                                             workingSetId, trackId))
                    {
                        return transaction.rollback();
                    }
                }
                return transaction.commit();
            }
            return false;
        }

        bool SqliteWorkingSetManager::removeFromWorkingSet(
            WorkingSetId workingSetId, TrackId trackId)
        {
            if (SqliteTransaction transaction{m_db})
            {
                if (transaction.execute("DELETE FROM WorkingSetTracks WHERE "
                                        "ws_id = ? AND track_id = ?;",
                                        workingSetId, trackId))
                {
                    return transaction.commit();
                }
            }
            return false;
        }

        bool SqliteWorkingSetManager::removeWorkingSet(
            WorkingSetId workingSetId)
        {
            if (SqliteTransaction transaction{m_db})
            {
                if (transaction.execute(
                        "DELETE FROM WorkingSetTracks WHERE ws_id = ?;",
                        workingSetId))
                {
                    if (transaction.execute(
                            "DELETE FROM WorkingSets WHERE ws_id = ?;",
                            workingSetId))
                    {
                        return transaction.commit();
                    }
                }
            }
            return false;
        }

    } // namespace database
} // namespace jucyaudio
