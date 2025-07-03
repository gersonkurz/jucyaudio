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
            if (m_db.isValid())
            {
                if (m_db.execute("BEGIN TRANSACTION;"))
                {
                    m_active = true;
                }
            }
            else
            {
                spdlog::error("SqliteTransaction: Database is not valid, cannot begin transaction.");
            }
        }

        SqliteTransaction::~SqliteTransaction()
        {
            if (m_active)
            {
                m_db.execute("ROLLBACK;");
            }
        }

        bool SqliteTransaction::commit()
        {
            if (!m_active)
                return false;
            m_active = false;
            return m_db.execute("COMMIT;");
        }

        bool SqliteTransaction::rollback()
        {
            if (m_active)
            {
                m_active = false;
                m_db.execute("ROLLBACK;");
            }
            return false;
        }

    } // namespace database
} // namespace jucyaudio