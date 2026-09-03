#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {

        SqliteTransaction::SqliteTransaction(SqliteDatabase &db)
            : m_db{db},
              m_active{false}
        {
            if (!m_db.isValid())
            {
                spdlog::error("SqliteTransaction: Database is not valid, cannot begin transaction.");
                return;
            }

            // Claim the connection before BEGIN, not after: between the two there would be a window in
            // which another thread could run a statement that this transaction would then own.
            m_connectionLock.emplace(m_db.getMutex());

            if (m_db.execute("BEGIN IMMEDIATE;"))
            {
                m_active = true;
            }
            else
            {
                // Nothing was begun, so nothing is owed the connection.
                releaseConnection();
            }
        }

        SqliteTransaction::~SqliteTransaction()
        {
            if (m_active)
            {
                // Last chance: commit() or rollback() may have failed and deliberately left the
                // transaction active for exactly this.
                if (!m_db.execute("ROLLBACK;"))
                {
                    // Nothing further can be done from here - the object is going away and the mutex
                    // cannot be held forever. Worth shouting about: the connection may still carry an
                    // open transaction, which the next writer on it will inherit.
                    spdlog::critical("SqliteTransaction: final ROLLBACK failed; the connection may still have an open transaction. DB error: {}",
                        m_db.getLastError());
                }
                m_active = false;
            }
            releaseConnection();
        }

        void SqliteTransaction::releaseConnection()
        {
            // Ordering matters at every call site: the terminal statement runs and succeeds first, and
            // only then is the connection handed back. Releasing while the transaction is still open in
            // SQLite would let another thread's statements join it.
            m_connectionLock.reset();
        }

        bool SqliteTransaction::commit()
        {
            if (!m_active)
                return false;

            if (!m_db.execute("COMMIT;"))
            {
                // SQLite can refuse a COMMIT - SQLITE_BUSY, most obviously - and leave the transaction
                // open. Clearing m_active here would strand it: the object would believe it was finished,
                // release the connection, and leave an open transaction for whoever writes next. Stay
                // active and keep the connection so the destructor, or an explicit rollback, can undo it.
                spdlog::error("SqliteTransaction: COMMIT failed, transaction left active for rollback. DB error: {}", m_db.getLastError());
                return false;
            }

            m_active = false;
            releaseConnection();
            return true;
        }

        bool SqliteTransaction::rollback()
        {
            if (m_active)
            {
                if (m_db.execute("ROLLBACK;"))
                {
                    m_active = false;
                    releaseConnection();
                }
                else
                {
                    // Same reasoning as a failed commit: the transaction is still open, so this object
                    // still owns it and still owns the connection. The destructor retries.
                    spdlog::error("SqliteTransaction: ROLLBACK failed, transaction left active for the destructor to retry. DB error: {}",
                        m_db.getLastError());
                }
            }
            else
            {
                releaseConnection();
            }

            // this must return false independent of the success of the action, because it signals to the caller
            // that a transaction has been aborted.
            return false;
        }

    } // namespace database
} // namespace jucyaudio