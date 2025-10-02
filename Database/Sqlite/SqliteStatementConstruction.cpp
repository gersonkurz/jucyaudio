#include <Database/Includes/Constants.h>
#include <Database/Sqlite/SqliteStatementConstruction.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/TrackLibrary.h>
#include <UI/Settings.h>
#include <Utils/AssortedUtils.h>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        SqliteStatementConstruction::SqliteStatementConstruction(SqliteStatement &stmt)
            : m_stmt(stmt),
              m_paramIndex(1)
        {
        }

        void SqliteStatementConstruction::addWhereClause(StringWriter &writer, const TrackQueryArgs &args)
        {
            bool whereAdded = false;

            auto addCondition = [&](const std::string &condition)
            {
                if (!whereAdded)
                {
                    writer.append(" WHERE ");
                    whereAdded = true;
                }
                else
                {
                    writer.append(" AND ");
                }
                writer.append(condition);
            };

            // Note: Search terms are now handled via FTS5 in createSelectStatement/createCountStatement
            // so we don't add them to the WHERE clause here
            
            // Add offline filtering if enabled in settings
            if (!config::theSettings.uiSettings.showOfflineTracks)
            {
                // Filter out tracks from offline folders
                addCondition("folder_id NOT IN (SELECT folder_id FROM temp.OfflineFolders)");
            }

            if (args.workingSetId > 0)
            {
                addCondition(std::format("track_id IN (SELECT track_id FROM WorkingSetTracks WHERE ws_id = ?{})", m_paramIndex++));
            }
            if (args.mixId > 0)
            {
                addCondition(std::format("track_id IN (SELECT track_id FROM MixTracks WHERE mix_id = ?{})", m_paramIndex++));
            }
            if (!args.folderIds.empty())
            {
                StringWriter folderCondition;
                
                if (args.recursive)
                {
                    // When recursive, we need to get all child folders too
                    // Import necessary header at top of file for folder database access
                    const auto& folderDb = theTrackLibrary.getFolderDatabase();
                    auto allFolderIds = folderDb.getAllChildFolders(args.folderIds);
                    
                    folderCondition.append("folder_id IN (");
                    for(size_t idx = 0; idx < allFolderIds.size(); ++idx)
                    {
                        folderCondition.append(idx == 0 ? std::format("?{}", m_paramIndex++) : std::format(", ?{}", m_paramIndex++));
                    }
                    folderCondition.append(")");
                }
                else
                {
                    // Non-recursive: just use the provided folder IDs
                    folderCondition.append("folder_id IN (");
                    for (size_t i = 0; i < args.folderIds.size(); ++i)
                    {
                        folderCondition.append(i == 0 ? std::format("?{}", m_paramIndex++) : std::format(", ?{}", m_paramIndex++));
                    }
                    folderCondition.append(")");
                }
                
                const auto conditionAsString{folderCondition.asString()};
                addCondition(conditionAsString);
            }
        }

        void SqliteStatementConstruction::addOrderByClause(StringWriter &writer, const TrackQueryArgs &args)
        {
            if (args.sortBy.empty())
                return;
            writer.append(" ORDER BY ");
            for (size_t i = 0; i < args.sortBy.size(); ++i)
            {
                writer.append(i == 0 ? "" : ", ");
                writer.append(args.sortBy[i].columnName);
                writer.append(args.sortBy[i].descending ? " COLLATE NOCASE DESC" : " COLLATE NOCASE ASC");
            }
        }

        bool SqliteStatementConstruction::finalizeStatement(StringWriter &writer, const TrackQueryArgs &args, WorkingSetId wsId)
        {
            // The logic for INSERT...SELECT needs a closing parenthesis
            if (wsId > 0)
            {
                writer.append(")");
            }

            const auto sqlStatement = writer.asString();
            if (!m_stmt.bindStatement(sqlStatement))
            {
                spdlog::error("Failed to bind statement: {}", sqlStatement);
                return false;
            }

            // Bind parameters in the exact order they were added
            if (wsId > 0)
            {
                m_stmt.addParam(wsId);
            }
            // FTS5 search term binding is now handled directly in the create methods
            if (!args.searchTerms.empty() && !args.searchTerms[0].empty())
            {
                spdlog::info("Binding FTS5 search parameter: '{}'", args.searchTerms[0]);
                m_stmt.addParam(args.searchTerms[0]);
            }
            if (args.workingSetId > 0)
            {
                m_stmt.addParam(args.workingSetId);
            }
            if (args.mixId > 0)
            {
                m_stmt.addParam(args.mixId);
            }
            // For folder IDs, we need to handle recursive case differently
            if (!args.folderIds.empty())
            {
                if (args.recursive)
                {
                    // When recursive, bind all child folder IDs
                    const auto& folderDb = theTrackLibrary.getFolderDatabase();
                    auto allFolderIds = folderDb.getAllChildFolders(args.folderIds);
                    for (const auto &folderId : allFolderIds)
                    {
                        m_stmt.addParam(folderId);
                    }
                }
                else
                {
                    // Non-recursive: just bind the provided folder IDs
                    for (const auto &folderId : args.folderIds)
                    {
                        m_stmt.addParam(folderId);
                    }
                }
            }
            return true;
        }

        bool SqliteStatementConstruction::createSelectStatement(const TrackQueryArgs &args)
        {
            m_paramIndex = 1;
            StringWriter writer;
            
            // If we have search terms, use FTS5 for searching
            if (!args.searchTerms.empty() && !args.searchTerms[0].empty())
            {
                spdlog::info("FTS5 Search: Using search term: '{}'", args.searchTerms[0]);
                writer.append("SELECT ");
                if (args.columns.empty())
                {
                    writer.append("Tracks.*");
                }
                else
                {
                    for (size_t i = 0; i < args.columns.size(); ++i)
                    {
                        writer.append(i == 0 ? "" : ", ");
                        writer.append("Tracks.");
                        writer.append(args.columns[i]);
                    }
                }
                // FTS5 with content table - just join on track_id directly
                writer.append(" FROM Tracks INNER JOIN TracksSearchFTS ON Tracks.track_id = TracksSearchFTS.rowid");
                writer.appendFormatted(" WHERE TracksSearchFTS MATCH ?{}", m_paramIndex++);
                
                // Add other WHERE conditions if needed
                if (args.workingSetId > 0)
                {
                    writer.appendFormatted(" AND track_id IN (SELECT track_id FROM WorkingSetTracks WHERE ws_id = ?{})", m_paramIndex++);
                }
                if (args.mixId > 0)
                {
                    writer.appendFormatted(" AND track_id IN (SELECT track_id FROM MixTracks WHERE mix_id = ?{})", m_paramIndex++);
                }
                if (!args.folderIds.empty())
                {
                    writer.append(" AND folder_id IN (");
                    for (size_t i = 0; i < args.folderIds.size(); ++i)
                    {
                        writer.append(i == 0 ? std::format("?{}", m_paramIndex++) : std::format(", ?{}", m_paramIndex++));
                    }
                    writer.append(")");
                }
            }
            else
            {
                // No search terms, use regular query
                writer.append("SELECT ");
                if (args.columns.empty())
                {
                    writer.append("*");
                }
                else
                {
                    for (size_t i = 0; i < args.columns.size(); ++i)
                    {
                        writer.append(i == 0 ? "" : ", ");
                        writer.append(args.columns[i]);
                    }
                }
                writer.append(" FROM Tracks");
                addWhereClause(writer, args);
            }
            
            addOrderByClause(writer, args);
            if (args.usePaging)
            {
                writer.appendFormatted(" LIMIT {} OFFSET {}", QUERY_PAGE_SIZE, args.offset);
            }
            
            // Log the complete SQL statement before finalizing
            const auto sqlPreview = writer.asString();
            spdlog::info("FTS5 Search SQL: {}", sqlPreview);
            spdlog::info("FTS5 Search will bind {} parameters", m_paramIndex - 1);
            
            return finalizeStatement(writer, args);
        }

        bool SqliteStatementConstruction::createCountStatement(const TrackQueryArgs &args)
        {
            m_paramIndex = 1;
            StringWriter writer;
            
            // If we have search terms, use FTS5 for searching
            if (!args.searchTerms.empty() && !args.searchTerms[0].empty())
            {
                spdlog::info("FTS5 Count: Using search term: '{}'", args.searchTerms[0]);
                writer.append("SELECT COUNT(*) FROM Tracks INNER JOIN TracksSearchFTS ON Tracks.track_id = TracksSearchFTS.rowid");
                writer.appendFormatted(" WHERE TracksSearchFTS MATCH ?{}", m_paramIndex++);
                
                // Add other WHERE conditions if needed
                if (args.workingSetId > 0)
                {
                    writer.appendFormatted(" AND track_id IN (SELECT track_id FROM WorkingSetTracks WHERE ws_id = ?{})", m_paramIndex++);
                }
                if (args.mixId > 0)
                {
                    writer.appendFormatted(" AND track_id IN (SELECT track_id FROM MixTracks WHERE mix_id = ?{})", m_paramIndex++);
                }
                if (!args.folderIds.empty())
                {
                    writer.append(" AND folder_id IN (");
                    for (size_t i = 0; i < args.folderIds.size(); ++i)
                    {
                        writer.append(i == 0 ? std::format("?{}", m_paramIndex++) : std::format(", ?{}", m_paramIndex++));
                    }
                    writer.append(")");
                }
            }
            else
            {
                writer.append("SELECT COUNT(*) FROM Tracks");
                addWhereClause(writer, args);
            }
            
            return finalizeStatement(writer, args);
        }

        bool SqliteStatementConstruction::createAggregateStatement(const TrackQueryArgs &args)
        {
            m_paramIndex = 1;
            StringWriter writer;

            // If we have search terms, use FTS5 for searching
            if (!args.searchTerms.empty() && !args.searchTerms[0].empty())
            {
                writer.append("SELECT COUNT(*), COALESCE(SUM(filesize_bytes), 0), COALESCE(SUM(duration), 0) ");
                writer.append("FROM Tracks INNER JOIN TracksSearchFTS ON Tracks.track_id = TracksSearchFTS.rowid");
                writer.appendFormatted(" WHERE TracksSearchFTS MATCH ?{}", m_paramIndex++);

                // Add other WHERE conditions if needed
                if (args.workingSetId > 0)
                {
                    writer.appendFormatted(" AND track_id IN (SELECT track_id FROM WorkingSetTracks WHERE ws_id = ?{})", m_paramIndex++);
                }
                if (args.mixId > 0)
                {
                    writer.appendFormatted(" AND track_id IN (SELECT track_id FROM MixTracks WHERE mix_id = ?{})", m_paramIndex++);
                }
                if (!args.folderIds.empty())
                {
                    writer.append(" AND folder_id IN (");
                    for (size_t i = 0; i < args.folderIds.size(); ++i)
                    {
                        writer.append(i == 0 ? std::format("?{}", m_paramIndex++) : std::format(", ?{}", m_paramIndex++));
                    }
                    writer.append(")");
                }
            }
            else
            {
                writer.append("SELECT COUNT(*), COALESCE(SUM(filesize_bytes), 0), COALESCE(SUM(duration), 0) FROM Tracks");
                addWhereClause(writer, args);
            }

            return finalizeStatement(writer, args);
        }

        bool SqliteStatementConstruction::createInsertIntoSelectTrackIdsStatement(const TrackQueryArgs &args, WorkingSetId wsId)
        {
            m_paramIndex = 1;
            StringWriter writer;
            
            // If we have search terms, use FTS5 for searching
            if (!args.searchTerms.empty() && !args.searchTerms[0].empty())
            {
                writer.appendFormatted("INSERT INTO WorkingSetTracks (ws_id, track_id) SELECT ?{}, Tracks.track_id FROM Tracks INNER JOIN TracksSearchFTS ON Tracks.track_id = TracksSearchFTS.rowid", m_paramIndex++);
                writer.appendFormatted(" WHERE TracksSearchFTS MATCH ?{}", m_paramIndex++);
                
                // Add other WHERE conditions if needed
                if (args.workingSetId > 0)
                {
                    writer.appendFormatted(" AND track_id IN (SELECT track_id FROM WorkingSetTracks WHERE ws_id = ?{})", m_paramIndex++);
                }
                if (args.mixId > 0)
                {
                    writer.appendFormatted(" AND track_id IN (SELECT track_id FROM MixTracks WHERE mix_id = ?{})", m_paramIndex++);
                }
                if (!args.folderIds.empty())
                {
                    writer.append(" AND folder_id IN (");
                    for (size_t i = 0; i < args.folderIds.size(); ++i)
                    {
                        writer.append(i == 0 ? std::format("?{}", m_paramIndex++) : std::format(", ?{}", m_paramIndex++));
                    }
                    writer.append(")");
                }
                // No need for closing parenthesis in FTS5 case
                const auto sqlStatement = writer.asString();
                if (!m_stmt.bindStatement(sqlStatement))
                {
                    spdlog::error("Failed to bind statement: {}", sqlStatement);
                    return false;
                }
                // Bind parameters
                m_stmt.addParam(wsId);
                m_stmt.addParam(args.searchTerms[0]);
                if (args.workingSetId > 0)
                {
                    m_stmt.addParam(args.workingSetId);
                }
                if (args.mixId > 0)
                {
                    m_stmt.addParam(args.mixId);
                }
                for (const auto &folderId : args.folderIds)
                {
                    m_stmt.addParam(folderId);
                }
                return true;
            }
            else
            {
                writer.appendFormatted("INSERT INTO WorkingSetTracks (ws_id, track_id) SELECT ?{}, track_id FROM (SELECT track_id FROM Tracks", m_paramIndex++);
                addWhereClause(writer, args);
                // Note: `finalizeStatement` adds the closing parenthesis.
                return finalizeStatement(writer, args, wsId);
            }
        }
    } // namespace database
} // namespace jucyaudio