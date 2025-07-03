#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
#define DESCRIBE_SQLITE_RC(__CODE__)                                                                                                                           \
    case SQLITE_##__CODE__:                                                                                                                                    \
        return #__CODE__;

        static std::string sqlite3_code_as_string(int rc)
        {
            switch (rc)
            {
                DESCRIBE_SQLITE_RC(OK)
                DESCRIBE_SQLITE_RC(ERROR)
                DESCRIBE_SQLITE_RC(INTERNAL)
                DESCRIBE_SQLITE_RC(PERM)
                DESCRIBE_SQLITE_RC(ABORT)
                DESCRIBE_SQLITE_RC(BUSY)
                DESCRIBE_SQLITE_RC(LOCKED)
                DESCRIBE_SQLITE_RC(NOMEM)
                DESCRIBE_SQLITE_RC(READONLY)
                DESCRIBE_SQLITE_RC(INTERRUPT)
                DESCRIBE_SQLITE_RC(IOERR)
                DESCRIBE_SQLITE_RC(CORRUPT)
                DESCRIBE_SQLITE_RC(NOTFOUND)
                DESCRIBE_SQLITE_RC(FULL)
                DESCRIBE_SQLITE_RC(CANTOPEN)
                DESCRIBE_SQLITE_RC(PROTOCOL)
                DESCRIBE_SQLITE_RC(EMPTY)
                DESCRIBE_SQLITE_RC(SCHEMA)
                DESCRIBE_SQLITE_RC(TOOBIG)
                DESCRIBE_SQLITE_RC(CONSTRAINT)
                DESCRIBE_SQLITE_RC(MISMATCH)
                DESCRIBE_SQLITE_RC(MISUSE)
                DESCRIBE_SQLITE_RC(NOLFS)
                DESCRIBE_SQLITE_RC(AUTH)
                DESCRIBE_SQLITE_RC(FORMAT)
                DESCRIBE_SQLITE_RC(RANGE)
                DESCRIBE_SQLITE_RC(NOTADB)
                DESCRIBE_SQLITE_RC(ROW)
                DESCRIBE_SQLITE_RC(DONE)
            default:
                return std::to_string(rc);
            }
        }

        bool SqliteDatabase::execute(std::string_view statement)
        {
            //spdlog::debug("SqliteDatabase executing SQL: {}", statement);
            char *lpszErrorMessage = nullptr;
            int rc = sqlite3_exec(m_db, statement.data(), nullptr, 0, &lpszErrorMessage);
            if (rc != SQLITE_OK)
            {
                formatError(__LINE__, rc, "sqlite3_exec({}) failed with {}", statement, lpszErrorMessage);
                sqlite3_free(lpszErrorMessage);
                return false;
            }
            return true;
        }

        const char *SqliteDatabase::getVersion() const
        {
            return SQLITE_VERSION " [" SQLITE_SOURCE_ID "]";
        }

        SqliteDatabase::~SqliteDatabase()
        {
            close();
        }

        void SqliteDatabase::close()
        {
            if (m_db)
            {
                sqlite3_close(m_db);
                m_db = nullptr;
            }
        }

        bool SqliteDatabase::open(std::string_view filename)
        {
            close();
            int rc = sqlite3_open_v2(filename.data(), &m_db,
                                     SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, // NOMUTEX if handling threading externally
                                     nullptr);

            if (rc != SQLITE_OK)
            {
                formatError(__LINE__, rc, "sqlite3_open({}) failed", filename);
                m_db = nullptr;
                return false;
            }
            sqlite3_busy_timeout(m_db, 60000);
            return true;
        }

        bool SqliteDatabase::raiseError(int lno, int rc, std::string_view message)
        {
            const char *msg = sqlite3_errmsg(m_db);

            std::string output;
            output.append(sqlite3_code_as_string(rc));
            output.append(": ");
            if (msg)
                output.append(msg);
            output.append(" in ");
            output.append(__FILE__);
            output.append("(");
            output.append(std::to_string(lno));
            output.append("): ");
            output.append(message);
            m_lastErrorMessage = output;
            spdlog::error("{}", output);
            return false;
        }

        bool SqliteDatabase::doesTableExist(std::string_view name)
        {
            SqliteStatement stmt{*this, "SELECT name FROM sqlite_master WHERE type='table' and name=?;"};

            stmt.addParam(name);
            if (!stmt.execute())
                return false;
            return !stmt.isQueryEmpty();
        }
    } // namespace database

} // namespace jucyaudio
