#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/TrackQueryArgs.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Utils/StringWriter.h>

namespace jucyaudio
{
    namespace database
    {
        class SqliteStatementConstruction final
        {
        public:
            SqliteStatementConstruction(SqliteStatement &stmt);

            bool createCountStatement(const TrackQueryArgs &trackQueryArgs);
            bool createAggregateStatement(const TrackQueryArgs &trackQueryArgs);
            bool createSelectStatement(const TrackQueryArgs &trackQueryArgs);
            bool createInsertIntoSelectTrackIdsStatement(const TrackQueryArgs &trackQueryArgs, WorkingSetId wsId);

        private:
            SqliteStatement &m_stmt;
            int m_paramIndex; // Renamed for clarity from m_searchTermIndex

            void addWhereClause(StringWriter &writer, const TrackQueryArgs &trackQueryArgs);
            void addOrderByClause(StringWriter &writer, const TrackQueryArgs &trackQueryArgs);
            bool finalizeStatement(StringWriter &writer, const TrackQueryArgs &trackQueryArgs, WorkingSetId wsId = 0);
            bool populateQueryScopeFoldersTable(const std::vector<FolderId> &folderIds);
        };
    } // namespace database
} // namespace jucyaudio