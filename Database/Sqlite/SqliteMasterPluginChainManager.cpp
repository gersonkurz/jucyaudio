#include <Database/Sqlite/SqliteMasterPluginChainManager.h>

#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>

#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        SqliteMasterPluginChainManager::SqliteMasterPluginChainManager(SqliteDatabase &db)
            : m_db{db}
        {
        }

        std::vector<MasterPluginChainEntry> SqliteMasterPluginChainManager::loadChain() const
        {
            std::vector<MasterPluginChainEntry> entries;

            const auto sql = R"SQL(
                SELECT order_index, plugin_format, identifier, name, manufacturer, version, is_enabled, state_blob
                FROM MasterChainPlugins
                ORDER BY order_index ASC
            )SQL";

            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("MasterChain: Failed to prepare load query: {}", m_db.getLastError());
                return entries;
            }

            while (stmt.getNextResult())
            {
                MasterPluginChainEntry entry{};
                entry.orderIndex = stmt.getInt32(0);
                entry.pluginFormat = stmt.isNull(1) ? std::string{} : stmt.getText(1);
                entry.identifier = stmt.isNull(2) ? std::string{} : stmt.getText(2);
                entry.name = stmt.isNull(3) ? std::string{} : stmt.getText(3);
                entry.manufacturer = stmt.isNull(4) ? std::string{} : stmt.getText(4);
                entry.version = stmt.isNull(5) ? std::string{} : stmt.getText(5);
                entry.isEnabled = stmt.getInt32(6) != 0;
                entry.stateBlob = stmt.isNull(7) ? std::vector<unsigned char>{} : stmt.getBlob(7);
                entries.emplace_back(std::move(entry));
            }

            return entries;
        }

        bool SqliteMasterPluginChainManager::saveChain(const std::vector<MasterPluginChainEntry> &entries) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                if (!m_db.execute("DELETE FROM MasterChainPlugins;"))
                {
                    transaction.rollback();
                    spdlog::error("MasterChain: Failed to clear chain table: {}", m_db.getLastError());
                    return false;
                }

                const auto insertSql = R"SQL(
                    INSERT INTO MasterChainPlugins (
                        order_index, plugin_format, identifier, name, manufacturer, version, is_enabled, state_blob
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                )SQL";

                for (const auto &entry : entries)
                {
                    SqliteStatement insertStmt{m_db, insertSql};
                    if (!insertStmt.isValid())
                    {
                        transaction.rollback();
                        spdlog::error("MasterChain: Failed to prepare insert: {}", m_db.getLastError());
                        return false;
                    }

                    insertStmt.addParam(entry.orderIndex);
                    insertStmt.addParam(entry.pluginFormat);
                    insertStmt.addParam(entry.identifier);
                    insertStmt.addParam(entry.name);
                    insertStmt.addParam(entry.manufacturer);
                    insertStmt.addParam(entry.version);
                    insertStmt.addParam(entry.isEnabled ? 1 : 0);
                    insertStmt.addParam(entry.stateBlob);

                    if (!insertStmt.execute())
                    {
                        transaction.rollback();
                        spdlog::error(
                            "MasterChain: Failed to insert entry '{}': {}",
                            entry.name,
                            m_db.getLastError());
                        return false;
                    }
                }

                if (!transaction.commit())
                {
                    spdlog::error("MasterChain: Failed to commit chain save: {}", m_db.getLastError());
                    return false;
                }

                return true;
            }

            spdlog::error("MasterChain: Failed to begin transaction for save");
            return false;
        }
    } // namespace database
} // namespace jucyaudio

