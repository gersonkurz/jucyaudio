#pragma once

#include <Database/Includes/ITagManager.h>
#include <Database/Includes/ITrackDatabase.h>
#include <Database/Sqlite/SqliteDatabase.h>

namespace jucyaudio
{
    namespace database
    {

        // If TagId and DbResult are in ITrackDatabase.h, ensure it's included.
        // Or, better, move common types like TagId, TrackId, DbResult, DbResultStatus
        // to a separate header like "DatabaseTypes.h" and have both ITrackDatabase and ITagManager include it.
        // For now, assuming ITrackDatabase.h is included.

        class SqliteTagManager : public ITagManager
        {
        public:
            SqliteTagManager(database::SqliteDatabase &db)
                : m_db{db}
            {
            }
            ~SqliteTagManager() override = default;

            std::optional<TagId> getOrCreateTagId(const std::string &tagName, bool createIfMissing = true) override;
            std::optional<std::string> getTagNameById(TagId tagId) const override;
            std::vector<TagInfo> getAllTags(const std::optional<std::string> &nameFilter = std::nullopt) const override;

        private:
            void buildCacheIfNeeded() const;

            database::SqliteDatabase &m_db;
            mutable std::unordered_map<TagId, std::string> m_tagIdToName;
            mutable std::unordered_map<std::string, TagId> m_tagNameToId;
        };

    } // namespace database
} // namespace jucyaudio
