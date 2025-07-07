#pragma once

#include <Database/Sqlite/sqlite3.h>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>

namespace jucyaudio
{
    namespace database
    {
        class SqliteDatabase final
        {
        public:
            SqliteDatabase()
                : m_db{nullptr}
            {
            }

            ~SqliteDatabase();

        public:
            const char *getVersion() const;
            bool open(std::string_view filename);
            void close();
            bool execute(std::string_view statement);

            bool isValid() const
            {
                return (m_db != nullptr);
            }

            bool doesTableExist(std::string_view name);

            auto getLastInsertRowId() const
            {
                return sqlite3_last_insert_rowid(m_db);
            }

            SqliteDatabase(const SqliteDatabase &) = delete;
            SqliteDatabase &operator=(const SqliteDatabase &) = delete;
            SqliteDatabase(SqliteDatabase &&) = delete;
            SqliteDatabase &operator=(SqliteDatabase &&) = delete;

            const auto &getLastError() const
            {
                return m_lastErrorMessage;
            }

            std::recursive_mutex &getMutex()
            {
                // This mutex is used to ensure thread safety for database operations
                // and should be locked before performing any operations on the database.
                return m_mutex;
            }

        private:
            friend class SqliteStatement;

        private:
            bool raiseError(int lno, int rc, std::string_view message);

            template <typename... Args> bool formatError(int lno, int rc, std::string_view text, Args &&...args)
            {
                return raiseError(lno, rc, std::vformat(text, std::make_format_args(args...)));
            }

        private:
            sqlite3 *m_db;
            mutable std::recursive_mutex m_mutex;
            mutable std::string m_lastErrorMessage;
        };

    } // namespace database
} // namespace jucyaudio
