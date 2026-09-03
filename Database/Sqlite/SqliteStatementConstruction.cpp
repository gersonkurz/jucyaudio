#include <Database/Includes/Constants.h>
#include <Database/Sqlite/SqliteStatementConstruction.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/TrackLibrary.h>
#include <UI/Settings.h>
#include <Utils/AssortedUtils.h>
#include <Utils/FilterParser.h>
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

        bool SqliteStatementConstruction::populateQueryScopeFoldersTable(const std::vector<FolderId> &folderIds)
        {
            // Get database from statement
            auto& db = m_stmt.getDatabase();

            // Create temp table if it doesn't exist
            SqliteStatement createStmt{db};
            createStmt.bindStatement("CREATE TEMP TABLE IF NOT EXISTS QueryScopeFolders (folder_id INTEGER PRIMARY KEY);");
            if (!createStmt.execute())
            {
                spdlog::error("populateQueryScopeFoldersTable: Failed to create temp table");
                return false;
            }

            // Clear existing contents
            SqliteStatement clearStmt{db};
            clearStmt.bindStatement("DELETE FROM temp.QueryScopeFolders;");
            if (!clearStmt.execute())
            {
                spdlog::error("populateQueryScopeFoldersTable: Failed to clear temp table");
                return false;
            }

            // Insert folder IDs in batches
            if (!folderIds.empty())
            {
                SqliteStatement insertStmt{db};
                insertStmt.bindStatement("INSERT INTO temp.QueryScopeFolders (folder_id) VALUES (?);");

                for (const auto folderId : folderIds)
                {
                    insertStmt.reset();
                    insertStmt.addParam(folderId);
                    if (!insertStmt.execute())
                    {
                        spdlog::error("populateQueryScopeFoldersTable: Failed to insert folder_id {}", folderId);
                        return false;
                    }
                }

                spdlog::debug("populateQueryScopeFoldersTable: Populated temp table with {} folder IDs", folderIds.size());
            }

            return true;
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
                // Populate temp table with folder IDs (handles both recursive and non-recursive)
                std::vector<FolderId> folderIdsToUse;

                if (args.recursive)
                {
                    // When recursive, get all child folders
                    const auto& folderDb = theTrackLibrary.getFolderDatabase();
                    auto allFolderIds = folderDb.getAllChildFolders(args.folderIds);
                    folderIdsToUse = std::vector<FolderId>{allFolderIds.begin(), allFolderIds.end()};
                }
                else
                {
                    // Non-recursive: just use the provided folder IDs
                    folderIdsToUse = args.folderIds;
                }

                // Populate the temp table with all folder IDs
                if (!const_cast<SqliteStatementConstruction*>(this)->populateQueryScopeFoldersTable(folderIdsToUse))
                {
                    spdlog::error("addWhereClause: Failed to populate QueryScopeFolders temp table");
                    return;
                }

                // Use the temp table in the WHERE clause
                addCondition("folder_id IN (SELECT folder_id FROM temp.QueryScopeFolders)");
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

        void SqliteStatementConstruction::addFilterCriteria(StringWriter &writer, const TrackQueryArgs &args)
        {
            if (args.filterCriteria.empty())
                return;

            for (const auto& criterion : args.filterCriteria)
            {
                // Map field name to SQL column
                const auto columnName = utils::mapFieldToColumn(criterion.fieldName);
                if (columnName.empty())
                {
                    spdlog::warn("Unknown filter field: {}", criterion.fieldName);
                    continue;
                }

                // Build the SQL condition using parameterized placeholders
                writer.append(" AND ");

                if (criterion.op == "..")
                {
                    // Range: field BETWEEN ?N AND ?N+1
                    const int firstParam = m_paramIndex++;
                    const int secondParam = m_paramIndex++;
                    writer.append(columnName);
                    writer.appendFormatted(" BETWEEN ?{} AND ?{}", firstParam, secondParam);
                    m_filterParams.push_back(criterion.value);
                    m_filterParams.push_back(criterion.value2);
                }
                else if (criterion.op == "=" && !utils::isNumericField(criterion.fieldName))
                {
                    // String exact match with LIKE for case-insensitive
                    writer.append(columnName);
                    writer.appendFormatted(" LIKE ?{}", m_paramIndex++);
                    m_filterParams.push_back(criterion.value);
                }
                else
                {
                    // Numeric comparison or string with operator
                    writer.append(columnName);
                    writer.append(" ");
                    writer.append(criterion.op);
                    writer.appendFormatted(" ?{}", m_paramIndex++);
                    m_filterParams.push_back(criterion.value);
                }

                spdlog::debug("Added filter SQL: {} {} {} (parameterized)", columnName, criterion.op, criterion.value);
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
            // Folder IDs are now handled via temp table, no need to bind them as parameters

            // Bind filter criteria parameters (added via addFilterCriteria)
            for (const auto& filterValue : m_filterParams)
            {
                m_stmt.addParam(filterValue);
            }

            return true;
        }

        bool SqliteStatementConstruction::createSelectStatement(const TrackQueryArgs &args)
        {
            m_paramIndex = 1;
            m_filterParams.clear();
            StringWriter writer;
            
            // If we have search terms, use FTS5 for searching
            if (!args.searchTerms.empty() && !args.searchTerms[0].empty())
            {
                spdlog::info("FTS5 Search: Using search term: '{}'", args.searchTerms[0]);
                writer.append("SELECT ");
                if (args.columns.empty())
                {
                    writer.append(trackColumnsForDecoding);
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
                    // Populate temp table with folder IDs
                    std::vector<FolderId> folderIdsToUse;
                    if (args.recursive)
                    {
                        const auto& folderDb = theTrackLibrary.getFolderDatabase();
                        auto allFolderIds = folderDb.getAllChildFolders(args.folderIds);
                        folderIdsToUse = std::vector<FolderId>{allFolderIds.begin(), allFolderIds.end()};
                    }
                    else
                    {
                        folderIdsToUse = args.folderIds;
                    }

                    if (!populateQueryScopeFoldersTable(folderIdsToUse))
                    {
                        spdlog::error("createSelectStatement(FTS): Failed to populate QueryScopeFolders temp table");
                        return false;
                    }

                    writer.append(" AND folder_id IN (SELECT folder_id FROM temp.QueryScopeFolders)");
                }

                // Add filter criteria (e.g., year:1991, bpm:>120)
                addFilterCriteria(writer, args);
            }
            else
            {
                // No search terms, use regular query
                writer.append("SELECT ");
                if (args.columns.empty())
                {
                    writer.append(trackColumnsForDecoding);
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

                // Add filter criteria (e.g., year:1991, bpm:>120)
                addFilterCriteria(writer, args);
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
            m_filterParams.clear();
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
                    // Populate temp table with folder IDs
                    std::vector<FolderId> folderIdsToUse;
                    if (args.recursive)
                    {
                        const auto& folderDb = theTrackLibrary.getFolderDatabase();
                        auto allFolderIds = folderDb.getAllChildFolders(args.folderIds);
                        folderIdsToUse = std::vector<FolderId>{allFolderIds.begin(), allFolderIds.end()};
                    }
                    else
                    {
                        folderIdsToUse = args.folderIds;
                    }

                    if (!populateQueryScopeFoldersTable(folderIdsToUse))
                    {
                        spdlog::error("createCountStatement(FTS): Failed to populate QueryScopeFolders temp table");
                        return false;
                    }

                    writer.append(" AND folder_id IN (SELECT folder_id FROM temp.QueryScopeFolders)");
                }

                // Add filter criteria
                addFilterCriteria(writer, args);
            }
            else
            {
                writer.append("SELECT COUNT(*) FROM Tracks");
                addWhereClause(writer, args);

                // Add filter criteria
                addFilterCriteria(writer, args);
            }

            return finalizeStatement(writer, args);
        }

        bool SqliteStatementConstruction::createAggregateStatement(const TrackQueryArgs &args)
        {
            m_paramIndex = 1;
            m_filterParams.clear();
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
                    // Populate temp table with folder IDs
                    std::vector<FolderId> folderIdsToUse;
                    if (args.recursive)
                    {
                        const auto& folderDb = theTrackLibrary.getFolderDatabase();
                        auto allFolderIds = folderDb.getAllChildFolders(args.folderIds);
                        folderIdsToUse = std::vector<FolderId>{allFolderIds.begin(), allFolderIds.end()};
                    }
                    else
                    {
                        folderIdsToUse = args.folderIds;
                    }

                    if (!populateQueryScopeFoldersTable(folderIdsToUse))
                    {
                        spdlog::error("createAggregateStatement(FTS): Failed to populate QueryScopeFolders temp table");
                        return false;
                    }

                    writer.append(" AND folder_id IN (SELECT folder_id FROM temp.QueryScopeFolders)");
                }

                // Add filter criteria
                addFilterCriteria(writer, args);
            }
            else
            {
                writer.append("SELECT COUNT(*), COALESCE(SUM(filesize_bytes), 0), COALESCE(SUM(duration), 0) FROM Tracks");
                addWhereClause(writer, args);

                // Add filter criteria
                addFilterCriteria(writer, args);
            }

            return finalizeStatement(writer, args);
        }

        bool SqliteStatementConstruction::createInsertIntoSelectTrackIdsStatement(const TrackQueryArgs &args, WorkingSetId wsId)
        {
            m_paramIndex = 1;
            m_filterParams.clear();
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
                    // Populate temp table with folder IDs
                    std::vector<FolderId> folderIdsToUse;
                    if (args.recursive)
                    {
                        const auto& folderDb = theTrackLibrary.getFolderDatabase();
                        auto allFolderIds = folderDb.getAllChildFolders(args.folderIds);
                        folderIdsToUse = std::vector<FolderId>{allFolderIds.begin(), allFolderIds.end()};
                    }
                    else
                    {
                        folderIdsToUse = args.folderIds;
                    }

                    if (!populateQueryScopeFoldersTable(folderIdsToUse))
                    {
                        spdlog::error("createInsertIntoSelectTrackIdsStatement(FTS): Failed to populate QueryScopeFolders temp table");
                        return false;
                    }

                    writer.append(" AND folder_id IN (SELECT folder_id FROM temp.QueryScopeFolders)");
                }

                // Add filter criteria (was previously missing - caused filters to be ignored for FTS queries)
                addFilterCriteria(writer, args);

                const auto sqlStatement = writer.asString();
                if (!m_stmt.bindStatement(sqlStatement))
                {
                    spdlog::error("Failed to bind statement: {}", sqlStatement);
                    return false;
                }
                // Bind parameters - note: no folder IDs to bind since we use temp table
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
                // Bind filter criteria parameters
                for (const auto& filterValue : m_filterParams)
                {
                    m_stmt.addParam(filterValue);
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