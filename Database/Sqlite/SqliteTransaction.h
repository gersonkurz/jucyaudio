
#pragma once

#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <list>
#include <string>
#include <string_view>

namespace jucyaudio
{
    namespace database
    {
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
            SqliteDatabase &m_db;
            bool m_active{false};
        };

    } // namespace database
} // namespace jucyaudio
