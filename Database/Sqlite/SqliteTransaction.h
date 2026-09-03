#pragma once

#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <mutex>
#include <optional>
#include <string_view>

namespace jucyaudio
{
    namespace database
    {
        /**
         * @brief A write transaction that owns the connection for as long as it is open.
         *
         * A SQLite transaction belongs to the connection, not to the thread that began it, and this
         * application has several threads sharing one connection: the message thread, the background
         * task service, a thread per TaskDialog task, batch export, and the BPM analysis pool. So a
         * transaction here shuts out two different things, because there are two different hazards:
         *  - BEGIN IMMEDIATE takes the write lock up front, so writers on other connections and in
         *    other processes are excluded, and the transaction either starts or does not. A deferred
         *    BEGIN takes a read lock and can fail the upgrade after the reads have already happened.
         *  - Holding SqliteDatabase::getMutex() until commit or rollback excludes the other threads
         *    sharing this connection, which BEGIN IMMEDIATE does nothing about. Without it, a statement
         *    another thread ran between BEGIN and COMMIT was part of this transaction and was committed
         *    or discarded along with it - and that thread's own BEGIN was refused outright, because the
         *    connection was already inside one.
         *
         * The mutex is recursive, so this transaction's own statements acquire it freely, and so does a
         * caller that already holds it.
         *
         * The cost: every other database user on this connection waits until this transaction ends,
         * rather than between its statements. Keep transactions short - a bounded loop of prepared
         * statements over data already in memory, with no file I/O and no waiting on another thread
         * inside. Every transaction in the project is that shape; the scanner commits per track.
         */
        class SqliteTransaction final
        {
        public:
            explicit SqliteTransaction(SqliteDatabase &db);
            ~SqliteTransaction();

            bool commit();
            bool rollback();

            operator bool() const
            {
                return m_active;
            }

            template <typename... Args> bool execute(std::string_view sql, Args &&...args)
            {
                if (!m_active)
                {
                    // Nothing is begun, so nothing may run through here. BEGIN IMMEDIATE can be refused
                    // with SQLITE_BUSY, and a statement issued after that would autocommit on its own,
                    // outside the transaction the caller believes it is in - a caller that does not
                    // check its transaction would then delete the row and report failure.
                    spdlog::error("SqliteTransaction: execute() refused, no transaction is open.");
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
            /// @brief Engaged for exactly as long as the transaction is open.
            std::optional<std::unique_lock<std::recursive_mutex>> m_connectionLock;
            bool m_active{false};
        };

    } // namespace database
} // namespace jucyaudio
