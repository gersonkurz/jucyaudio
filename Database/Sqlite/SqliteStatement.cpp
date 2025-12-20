#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteDatabase.h>

namespace jucyaudio
{
    namespace database
    {
        SqliteStatement::SqliteStatement(SqliteDatabase &db, std::string_view statement)
            : m_lock{db.getMutex()},
              m_db{db},
              m_statement{nullptr},
              m_param_index{1},
              m_statement_text{},
              m_done{false}
        {
            bindStatement(statement);
        }

        SqliteStatement::SqliteStatement(SqliteDatabase &db)
            : m_lock{db.getMutex()},
              m_db{db},
              m_statement{nullptr},
              m_param_index{1},
              m_done{false}
        {
        }

        bool SqliteStatement::bindColumnFrom(const SqliteStatement &sourceStmt, int columnIndex)
        {
            if (!sourceStmt.isValid())
            {
                return false;
            }

            const int columnType = sourceStmt.getColumnType(columnIndex);

            switch (columnType)
            {
            case SQLITE_INTEGER:
                return addParam(sourceStmt.getInt64(columnIndex));
            case SQLITE_FLOAT:
                return addParam(sourceStmt.getFloat(columnIndex));
            case SQLITE_TEXT:
                return addParam(sourceStmt.getText(columnIndex));
            case SQLITE_BLOB:
                return addParam(sourceStmt.getBlob(columnIndex));
            case SQLITE_NULL:
                return addNullParam();
            default:
                // This should not happen with standard types.
                spdlog::error("Unsupported column type {} in bindColumnFrom", columnType);
                return false;
            }
        }

        void SqliteStatement::reset()
        {
            if (m_statement)
            {
                sqlite3_reset(m_statement);
                sqlite3_clear_bindings(m_statement);
                m_param_index = 1;
                m_done = false;
                m_copy_of_string_args.clear();
            }
        }

        bool SqliteStatement::bindStatement(std::string_view statement)
        {
            assert(!m_statement);
            assert(m_statement_text.empty());

            //spdlog::debug("SqliteStatement executing SQL: {}", statement);

            m_statement_text = statement;
            m_param_index = 1;
            m_done = false;

            int rc;
            do
            {
                rc = sqlite3_prepare_v2(m_db.m_db, m_statement_text.data(), (int)m_statement_text.length(), &m_statement, nullptr);
                if (rc == SQLITE_BUSY)
                {
                    std::this_thread::yield();
                }
                else if (rc)
                {
                    m_statement = nullptr;
                    return m_db.formatError(__LINE__, rc, "sqlite3_prepare_v2({}) failed", m_statement_text);
                }
            } while (rc == SQLITE_BUSY);

            return true;
        }

        bool SqliteStatement::getNextResult()
        {
            if (m_done)
                return false;
            for (;;)
            {
                const int rc = sqlite3_step(m_statement);
                if (rc == SQLITE_DONE)
                {
                    m_done = true;
                    return false; // No more rows
                }
                if (rc == SQLITE_ROW)
                    return true;

                m_done = true;
                return m_db.formatError(__LINE__, rc, "SqliteStatement::get_next_result({}) failed", m_statement_text);
            }
        }

        bool SqliteStatement::addNullParam()
        {
            //spdlog::debug("addParam null at {}", m_param_index);
            const int rc = sqlite3_bind_null(m_statement, m_param_index++);
            if (rc)
            {
                return m_db.raiseError(__LINE__, rc, "sqlite3_bind_null() failed");
            }
            return true;
        }

        bool SqliteStatement::addParam(const std::vector<unsigned char> &blob)
        {
            if (blob.empty())
            {
                // Bind zero-length blob (not NULL) - use nullptr with size 0
                const int rc = sqlite3_bind_blob(m_statement, m_param_index++, nullptr, 0, SQLITE_STATIC);
                if (rc)
                {
                    return m_db.raiseError(__LINE__, rc, "sqlite3_bind_blob() failed");
                }
                return true;
            }
            const int rc = sqlite3_bind_blob(m_statement, m_param_index++, blob.data(), (int)blob.size(), SQLITE_STATIC);
            if (rc)
            {
                return m_db.raiseError(__LINE__, rc, "sqlite3_bind_blob() failed");
            }
            return true;
        }

        bool SqliteStatement::addParam(std::string_view text)
        {
            //spdlog::debug("addParam text \"{}\" at {}", text, m_param_index);
            m_copy_of_string_args.push_back(std::string{text});
            const auto &item{m_copy_of_string_args.back()};

            const int rc = sqlite3_bind_text(m_statement, m_param_index++, item.c_str(), (int)item.length(), nullptr);
            if (rc)
            {
                return m_db.formatError(__LINE__, rc, "sqlite3_bind_text({}) failed", item);
            }
            return true;
        }

        bool SqliteStatement::addNullableParam(const std::string& text)
        {
            if (text.empty())
            {
                return addNullParam();
            }
            return addParam(std::string_view{text});
        }

        bool SqliteStatement::addParam(int64_t value)
        {
            //spdlog::debug("addParam int64_t {} at {}", value, m_param_index);
            const int rc = sqlite3_bind_int64(m_statement, m_param_index++, value);
            if (rc)
            {
                return m_db.formatError(__LINE__, rc, "sqlite3_bind_int64({}) failed", value);
            }
            return true;
        }

        bool SqliteStatement::addParam(int32_t value)
        {
            //spdlog::debug("addParam int32_t {} at {}", value, m_param_index);
            const int rc = sqlite3_bind_int(m_statement, m_param_index++, value);
            if (rc)
            {
                return m_db.formatError(__LINE__, rc, "sqlite3_bind_int({}) failed", value);
            }
            return true;
        }

        bool SqliteStatement::addParam(double value)
        {
            //spdlog::debug("addParam double {} at {}", value, m_param_index);
            const int rc = sqlite3_bind_double(m_statement, m_param_index++, value);
            if (rc)
            {
                return m_db.formatError(__LINE__, rc, "sqlite3_bind_int({}) failed", value);
            }
            return true;
        }

        bool SqliteStatement::addParam(nullptr_t)
        {
            //spdlog::debug("addParam nullptr_t at {}", m_param_index);
            const int rc = sqlite3_bind_null(m_statement, m_param_index++);
            if (rc)
            {
                return m_db.raiseError(__LINE__, rc, "sqlite3_bind_null() failed");
            }
            return true;
        }

        
        std::string SqliteStatement::getText(int index) const
        {
            auto p = reinterpret_cast<const char *>(sqlite3_column_text(m_statement, index));
            if (p)
                return std::string{p};

            return {};
        }

        std::vector<unsigned char> SqliteStatement::getBlob(int index) const
        {
            const auto p = reinterpret_cast<const unsigned char *>(sqlite3_column_blob(m_statement, index));
            int size = sqlite3_column_bytes(m_statement, index);

            std::vector<unsigned char> result;
            if (size > 0)
            {
                result.resize(size);
                memcpy(&result[0], p, size);
            }
            return result;
        }

        bool SqliteStatement::execute()
        {
            m_done = false;
            for (;;)
            {
                const int rc = sqlite3_step(m_statement);
                if (rc == SQLITE_ROW)
                {
                    return true;
                }
                if (rc == SQLITE_DONE)
                {
                    m_done = true;
                    return true;
                }
                return m_db.formatError(__LINE__, rc, "SqliteStatement::execute({}) failed", m_statement_text);
            }
        }

    } // namespace database
} // namespace jucyaudio
