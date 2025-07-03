#include <Database/Includes/Constants.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTagManager.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {

        std::optional<TagId> SqliteTagManager::getOrCreateTagId(const std::string &tagName, bool createIfMissing)
        {
            if (tagName.empty())
            {
                spdlog::warn("SqliteTagManager::getOrCreateTagId: Attempted to get/create tag with empty name.");
                return std::nullopt;
            }
            buildCacheIfNeeded();
            // Check if tag already exists in cache
            auto it = m_tagNameToId.find(tagName);
            if (it != m_tagNameToId.end())
            {
                spdlog::debug("SqliteTagManager::getOrCreateTagId: Found tag '{}' in cache with ID {}.", tagName, it->second);
                return it->second; // Return cached ID
            }

            if (!createIfMissing)
            { // If not found in cache and not allowed to create
                // Optionally, do a SELECT from DB here just in case cache is stale,
                // but if cache is source of truth, this is enough.
                return std::nullopt;
            }
            if (SqliteTransaction transaction{m_db})
            {
                if (transaction.execute("INSERT OR IGNORE INTO Tags (name) VALUES (?1);", tagName))
                {
                    SqliteStatement stmt{m_db};
                    std::optional<TagId> result{std::nullopt};
                    if (stmt.query(
                            [this, &tagName, &stmt, &result]() -> bool
                            {
                                const auto updatedTagId = stmt.getInt64(0);
                                m_tagNameToId[tagName] = updatedTagId; // Cache the tag name to ID mapping
                                m_tagIdToName[updatedTagId] = tagName; // Cache the tag ID to name mapping
                                result = updatedTagId;
                                return true;
                            },
                            "SELECT tag_id FROM Tags WHERE name = ?1 COLLATE NOCASE;", tagName))
                    {
                        transaction.commit(); // Commit the transaction if successful
                        return result;
                    }
                }
            }
            return std::nullopt;
        }

        void SqliteTagManager::buildCacheIfNeeded() const
        {
            if (m_tagIdToName.empty() && m_tagNameToId.empty())
            {
                // Build cache from existing tags
                const std::vector<TagInfo> allTags{getAllTags()};
                for (const auto &tag : allTags)
                {
                    m_tagIdToName[tag.id] = tag.name;
                    m_tagNameToId[tag.name] = tag.id;
                }
                spdlog::debug("SqliteTagManager::buildCacheIfNeeded: Cache built with {} tags.", allTags.size());
            }
            else
            {
                spdlog::debug("SqliteTagManager::buildCacheIfNeeded: Cache already built, skipping.");
            }
        }

        std::optional<std::string> SqliteTagManager::getTagNameById(TagId tagId) const
        {
            if (tagId <= 0)
            {
                spdlog::warn("SqliteTagManager::getTagNameById: Invalid TagId {} provided.", tagId);
                return std::nullopt;
            }
            buildCacheIfNeeded();
            const auto it = m_tagIdToName.find(tagId);
            if (it != m_tagIdToName.end())
            {
                spdlog::debug("SqliteTagManager::getTagNameById: Found tag name '{}' for TagId {} in cache.", it->second, tagId);
                return it->second; // Return cached name
            }
            spdlog::error("SqliteTagManager::getTagNameById: Failed to prepare SELECT statement for TagId {}. DB error: {}", tagId, m_db.getLastError());
            return std::nullopt;
        }

        std::vector<TagInfo> SqliteTagManager::getAllTags(const std::optional<std::string> &nameFilter) const
        {
            std::vector<TagInfo> tags;
            if (!m_tagIdToName.empty() && !m_tagNameToId.empty())
            {
                // we can just return all tags from the cache
                for (const auto &[id, name] : m_tagIdToName)
                {
                    if (!nameFilter || (nameFilter && name.find(*nameFilter) != std::string::npos))
                    {
                        tags.emplace_back(TagInfo{id, name}); // Return as vector of TagInfo
                    }
                }
                return tags;
            }
            std::string query_string = "SELECT tag_id, name FROM Tags";

            if (nameFilter && !nameFilter->empty())
            {
                query_string += " WHERE name LIKE ?1 COLLATE NOCASE";
            }
            query_string += " ORDER BY name COLLATE NOCASE ASC;";

            // const_cast okay here.
            database::SqliteStatement stmt(const_cast<database::SqliteDatabase &>(m_db), query_string);
            if (!stmt.isValid())
            {
                spdlog::error("SqliteTagManager::getAllTags: Failed to prepare SELECT statement. DB error: {}", m_db.getLastError());
                return {};
            }

            if (nameFilter && !nameFilter->empty())
            {
                if (!stmt.addParam("%" + *nameFilter + "%"))
                {
                    spdlog::error("SqliteTagManager::getAllTags: Failed to bind nameFilter parameter. DB error: {}", m_db.getLastError());
                    return {};
                }
            }

            while (stmt.getNextResult())
            { // true means SQLITE_ROW
                TagId id = stmt.getInt64(0);
                std::string name = stmt.getText(1); // Assuming name is not null due to schema. Add isNull check if paranoid.
                tags.push_back({id, name});
            }

            // After loop, check if getNextResult() returned false due to an error or SQLITE_DONE.
            // m_done is true in both cases. m_db.getLastError() will contain message on error.
            if (!stmt.isQueryEmpty() /* this means m_done is false, only possible if loop never ran due to immediate error */ ||
                (stmt.isQueryEmpty() && !m_db.getLastError().empty()) /* m_done is true, but there was an error */)
            {
                spdlog::error("SqliteTagManager::getAllTags: Error during query execution or loop termination. DB error: {}", m_db.getLastError());
                // Depending on severity, you might clear 'tags' or return what was gathered.
            }

            return tags;
        }

    } // namespace database
} // namespace jucyaudio