#pragma once

#include <string>
#include <string_view>
#include <list>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Includes/Constants.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        class SqliteStatement final
        {
        public:
            SqliteStatement(SqliteDatabase &db, std::string_view statement);
            SqliteStatement(SqliteDatabase &db);

            SqliteStatement(const SqliteStatement &) = delete;
            SqliteStatement &operator=(const SqliteStatement &) = delete;
            SqliteStatement(SqliteStatement &&) = delete;
            SqliteStatement &operator=(SqliteStatement &&) = delete;

            ~SqliteStatement()
            {
                if (m_statement)
                {
                    sqlite3_finalize(m_statement);
                    m_statement = nullptr;
                }
            }

            /// @brief bind a statement explicitly
            /// @note: This method is designed *ONLY* to be used if you didn't pass a statement in the constructor
            /// @param statement 
            bool bindStatement(std::string_view statement);

        public:
            bool addNullParam();
            bool addParam(std::string_view text);
            bool addParam(int64_t value);
            bool addParam(int32_t value);
            bool addParam(nullptr_t parameter1);
            bool addParam(double value);
            bool addParam(const std::vector<unsigned char> &blob);
            bool execute();
            bool isQueryEmpty() const
            {
                return m_done;
            }
            bool getNextResult();
            bool isValid() const
            {
                return m_statement != nullptr;
            }

            bool isNull(int index) const
            {
                return sqlite3_column_type(m_statement, index) == SQLITE_NULL;
            }

            std::string get_column_name(int index) const
            {
                return sqlite3_column_name(m_statement, index);
            }

            int getColumnType(int index) const
            {
                return sqlite3_column_type(m_statement, index);
            }

            double_t getFloat(int index) const
            {
                return sqlite3_column_double(m_statement, index);
            }

            int64_t getInt64(int index) const
            {
                return sqlite3_column_int64(m_statement, index);
            }
            int32_t getInt32(int index) const
            {
                return sqlite3_column_int(m_statement, index);
            }

            std::string getText(int index) const;

            std::vector<unsigned char> getBlob(int index) const;

            size_t getNumberOfColumns() const
            {
                return sqlite3_column_count(m_statement);
            }

            // Add this to your SqliteStatement class or as a free function
            using Callback = std::function<bool()>;
            template <typename Callback, typename... Args> bool query(Callback &&callback, const std::string &sql, Args &&...args)
            {
                if (!bindStatement(sql))
                {
                    return false;
                }

                // Add all parameters with error checking using fold expression
                bool allParamsOk = (addParam(std::forward<Args>(args)) && ...);
                if (!allParamsOk)
                {
                    return false;
                }

                if (!isValid())
                {
                    return false;
                }

                // Execute callback for each result row
                while (getNextResult())
                {
                    if (!callback())
                    {
                        return false;
                    }
                }
                return true;
            }
        private:
            /// <summary>
            /// Note: we are using a list, so that the pointers do not change
            /// </summary>
            std::list<std::string> m_copy_of_string_args;
            std::lock_guard<std::recursive_mutex> m_lock;
            SqliteDatabase &m_db;
            sqlite3_stmt *m_statement;
            int m_param_index;
            std::string m_statement_text;
            bool m_done;
        };

    } // namespace database
} // namespace jucyaudio
