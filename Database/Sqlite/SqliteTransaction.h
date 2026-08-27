
#pragma once

#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace jucyaudio
{
    namespace database
    {
        /**
         * @brief How much of the world a transaction shuts out.
         *
         * The default is what this class has always done. Immediate exists for operations whose
         * correctness depends on nothing else changing underneath them - reading, deciding something
         * from what was read, and writing based on that decision.
         */
        enum class TransactionMode
        {
            /**
             * @brief BEGIN TRANSACTION. No connection ownership.
             *
             * SQLite takes a read lock and upgrades it on the first write, so the upgrade can fail with
             * SQLITE_BUSY after reads have already happened. Other threads sharing this connection can
             * also run statements between this transaction's BEGIN and COMMIT - and because a SQLite
             * transaction belongs to the connection rather than to a thread, those statements are part
             * of it, and are committed or discarded along with it.
             *
             * Fine for the short write-only sequences this is mostly used for. Not fine for anything
             * that reads, decides, and then writes.
             */
            Deferred,

            /**
             * @brief BEGIN IMMEDIATE, and ownership of the connection for the transaction's lifetime.
             *
             * Two different exclusions, because there are two different hazards:
             *  - BEGIN IMMEDIATE takes the write lock up front, so writers on other connections and in
             *    other processes are excluded, and the transaction either starts or does not.
             *  - Holding SqliteDatabase::getMutex() until commit or rollback excludes other threads
             *    sharing this connection, which BEGIN IMMEDIATE does nothing about.
             *
             * The mutex is recursive, so this transaction's own statements acquire it freely.
             *
             * The cost is real: every other database user on this connection waits. Keep immediate
             * transactions short.
             */
            Immediate
        };

        class SqliteTransaction final
        {
        public:
            explicit SqliteTransaction(SqliteDatabase &db, TransactionMode mode = TransactionMode::Deferred);
            ~SqliteTransaction();

            bool commit();
            bool rollback();

            operator bool() const
            {
                return m_active;
            }

            template <typename... Args> bool execute(std::string_view sql, Args &&...args)
            {
                if (!m_db.isValid())
                {
                    return false;
                }

                SqliteStatement stmt{m_db, sql};

                // Bind all parameters using fold expression with short-circuit evaluation
                bool allParamsOk = (stmt.addParam(std::forward<Args>(args)) && ...);

                return allParamsOk && stmt.execute();
            }

        private:
            /// @brief Let go of the connection, if this transaction had claimed it.
            /// Called from commit, rollback and the destructor - the mutex must not outlive the
            /// transaction, and must not be released before it.
            void releaseConnection();

            SqliteDatabase &m_db;
            /// @brief Engaged only in Immediate mode, for exactly as long as the transaction is open.
            std::optional<std::unique_lock<std::recursive_mutex>> m_connectionLock;
            bool m_active{false};
        };

    } // namespace database
} // namespace jucyaudio
