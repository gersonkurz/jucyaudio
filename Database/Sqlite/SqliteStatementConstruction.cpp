
#include <Database/Includes/Constants.h>
#include <Database/Sqlite/SqliteStatementConstruction.h>
#include <Utils/AssortedUtils.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {

        void SqliteStatementConstruction::addWhereClause(StringWriter &writer, const TrackQueryArgs &trackQueryArgs)
        {
            bool bWhereAdded = false;
            for (const auto &searchTerm : trackQueryArgs.searchTerms)
            {
                if (!searchTerm.empty())
                {
                    if (!bWhereAdded)
                    {
                        writer.append(" WHERE (");
                        bWhereAdded = true;
                    }
                    else
                    {
                        writer.append(" AND (");
                    }
                    writer.append_formatted("title LIKE ?{} OR artist_name LIKE ?{} OR album_title LIKE ?{} OR filepath LIKE ?{})",
                                            m_searchTermIndex, m_searchTermIndex, m_searchTermIndex, m_searchTermIndex, m_searchTermIndex);
                    ++m_searchTermIndex;
                }
            }
            if (trackQueryArgs.pathFilter.has_value())
            {
                if (!bWhereAdded)
                {
                    writer.append(" WHERE ");
                    bWhereAdded = true;
                }
                else
                {
                    writer.append(" AND ");
                }
                writer.append_formatted("filepath LIKE ?{}", m_searchTermIndex); // Assuming pathFilter is a string with
                ++m_searchTermIndex;
            }
            if (trackQueryArgs.workingSetId)
            {
                if (!bWhereAdded)
                {
                    writer.append(" WHERE ");
                    bWhereAdded = true;
                }
                else
                {
                    writer.append(" AND ");
                }
                writer.append_formatted("track_id IN (SELECT track_id FROM WorkingSetTracks WHERE ws_id = ?{})", m_searchTermIndex);
                ++m_searchTermIndex;
            }
            if (trackQueryArgs.mixId)
            {
                if (!bWhereAdded)
                {
                    writer.append(" WHERE ");
                    bWhereAdded = true;
                }
                else
                {
                    writer.append(" AND ");
                }
                writer.append_formatted("track_id IN (SELECT track_id FROM MixTracks WHERE mix_id = ?{})", m_searchTermIndex);
                ++m_searchTermIndex;
            }
        }

        void SqliteStatementConstruction::addOrderByClause(StringWriter &writer, const TrackQueryArgs &trackQueryArgs)
        {
            bool bOrderByAdded = false;
            for (const auto &orderCriterion : trackQueryArgs.sortBy)
            {
                if (!bOrderByAdded)
                {
                    writer.append(" ORDER BY ");
                    bOrderByAdded = true;
                }
                else
                {
                    writer.append(", ");
                }
                writer.append(orderCriterion.columnName);
                if (orderCriterion.descending)
                {
                    writer.append(" COLLATE NOCASE DESC");
                }
                else
                {
                    writer.append(" COLLATE NOCASE ASC");
                }
            }
        }

        bool SqliteStatementConstruction::finalizeStatement(StringWriter &writer, const TrackQueryArgs &trackQueryArgs, WorkingSetId wsId)
        {
            if (wsId)
            {
                writer.append(")"); // Close the WHERE clause if it was opened
            }
            const auto sqlStatement = writer.as_string();
            if (!m_stmt.bindStatement(sqlStatement))
            {
                spdlog::error("Failed to bind count statement: {}", sqlStatement);
                return false;
            }
            if (wsId)
            {
                m_stmt.addParam(wsId); // Bind each search term with wildcards
            }
            for (const auto &searchTerm : trackQueryArgs.searchTerms)
            {
                if (!searchTerm.empty())
                {
                    m_stmt.addParam("%" + searchTerm + "%"); // Bind each search term with wildcards
                }
            }
            if (trackQueryArgs.pathFilter.has_value())
            {
                m_stmt.addParam(pathToString(trackQueryArgs.pathFilter.value()) + "%"); // Bind path filter with wildcards
            }
            if (trackQueryArgs.workingSetId)
            {
                m_stmt.addParam(trackQueryArgs.workingSetId);
            }
            if (trackQueryArgs.mixId)
            {
                m_stmt.addParam(trackQueryArgs.mixId);
            }
            return true;
        }

        bool SqliteStatementConstruction::createSelectStatement(const TrackQueryArgs &trackQueryArgs)
        {
            m_searchTermIndex = 1;
            StringWriter writer;
            writer.append("SELECT * FROM Tracks");
            addWhereClause(writer, trackQueryArgs);
            addOrderByClause(writer, trackQueryArgs);
            if (trackQueryArgs.usePaging)
            {
                writer.append_formatted(" LIMIT {} OFFSET {}", QUERY_PAGE_SIZE, trackQueryArgs.offset);
            }
            return finalizeStatement(writer, trackQueryArgs);
        }

        bool SqliteStatementConstruction::createInsertIntoSelectTrackIdsStatement(const TrackQueryArgs &trackQueryArgs, WorkingSetId wsId)
        {
            m_searchTermIndex = 1;
            StringWriter writer;
            writer.append_formatted("INSERT INTO WorkingSetTracks (ws_id, track_id) SELECT ?{}, track_id FROM (SELECT track_id FROM Tracks", m_searchTermIndex);
            ++m_searchTermIndex;
            addWhereClause(writer, trackQueryArgs); // no sort order, because we're adding this into a new table
            return finalizeStatement(writer, trackQueryArgs, wsId);
        }

        bool SqliteStatementConstruction::createCountStatement(const TrackQueryArgs &trackQueryArgs)
        {
            m_searchTermIndex = 1;
            StringWriter writer;
            writer.append("SELECT COUNT(*) FROM Tracks");

            addWhereClause(writer, trackQueryArgs);
            return finalizeStatement(writer, trackQueryArgs);
        }
    } // namespace database
} // namespace jucyaudio