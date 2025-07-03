#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Database/Sqlite/SqliteMixManager.h>
#include <Utils/StringWriter.h>

namespace jucyaudio
{
    namespace database
    {
        class SqliteStatementConstruction final
        {
        public:
            SqliteStatementConstruction(SqliteStatement &stmt)
                : m_stmt{stmt},
                  m_searchTermIndex{1} // Start at 1 for the first search term placeholder
            {
            }

            bool createCountStatement(const TrackQueryArgs &trackQueryArgs);
            bool createSelectStatement(const TrackQueryArgs &trackQueryArgs);
            bool createInsertIntoSelectTrackIdsStatement(const TrackQueryArgs &trackQueryArgs, WorkingSetId wsId);

        private:
            SqliteStatement &m_stmt;
            int m_searchTermIndex = 1;

            void addWhereClause(StringWriter &writer, const TrackQueryArgs &trackQueryArgs);
            void addOrderByClause(StringWriter &writer, const TrackQueryArgs &trackQueryArgs);
            bool finalizeStatement(StringWriter &writer, const TrackQueryArgs &trackQueryArgs, WorkingSetId wsId = 0);
        };
    } // namespace database
} // namespace jucyaudio