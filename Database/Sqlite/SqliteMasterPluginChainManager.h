#pragma once

#include <Database/Includes/MasterPluginChainEntry.h>
#include <Database/Sqlite/SqliteDatabase.h>

#include <vector>

namespace jucyaudio
{
    namespace database
    {
        class SqliteMasterPluginChainManager final
        {
        public:
            explicit SqliteMasterPluginChainManager(SqliteDatabase &db);

            std::vector<MasterPluginChainEntry> loadChain() const;
            bool saveChain(const std::vector<MasterPluginChainEntry> &entries) const;

        private:
            SqliteDatabase &m_db;
        };
    } // namespace database
} // namespace jucyaudio

