#pragma once

#include <Database/Includes/IFolderDatabase.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <mutex>
#include <unordered_map>

namespace jucyaudio
{
    namespace database
    {

        class SqliteFolderDatabase : public IFolderDatabase
        {
        public:
            explicit SqliteFolderDatabase(database::SqliteDatabase &db);
            ~SqliteFolderDatabase() override = default;

            // --- IFolderDatabase Interface ---
            std::optional<FolderInfo> getFolderById(FolderId folderId) const override;
            std::vector<FolderInfo> getChildFolders(FolderId parentId) const override;
            bool hasChildren(FolderId parentId) const override;
            bool addFolder(FolderInfo &folder) override;
            bool removeFolder(FolderId folderId) override;
            bool updateFolder(const FolderInfo &folder) override;
            void invalidateCache() override;
            FolderId findOrCreateFolderByPath(const std::filesystem::path &path) override;

        private:
            /// @brief Loads all folders from the database into the cache if it's empty.
            void buildCacheIfNeeded() const;

            /// @brief Creates a FolderInfo struct from a database query result.
            static FolderInfo getFolderInfoFromStatement(class SqliteStatement &stmt);

            database::SqliteDatabase &m_db;

            // --- Caching Mechanism ---
            // The cache is mutable so it can be populated by const methods.
            mutable std::unordered_map<FolderId, FolderInfo> m_folderCache;
            // NEW: A secondary cache for fast parent->child lookups.
            mutable std::unordered_map<FolderId, std::vector<FolderId>> m_childIndexCache;
            mutable bool m_isCacheValid{false};
            mutable std::mutex m_cacheMutex;
        };
    } // namespace database
} // namespace jucyaudio