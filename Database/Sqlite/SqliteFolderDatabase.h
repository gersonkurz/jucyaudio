#pragma once

#include <Database/Includes/IFolderDatabase.h>
#include <Database/Includes/FolderInfo.h>
#include <Database/Sqlite/sqlite3.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        class SqliteFolderDatabase : public IFolderDatabase
        {
        public:
            SqliteFolderDatabase(database::SqliteDatabase &db)
                : m_db{db}
            {
            }
            ~SqliteFolderDatabase() override = default; // Important to override virtual destructor

        private:
            bool getFolders(std::vector<FolderInfo> &folders) const override;
            bool addFolder(FolderInfo &folder) override;
            bool removeFolder(FolderId folderIdToRemove) override;
            bool removeAllFolders() override;
            bool updateFolder(const FolderInfo &folder) override;

        private:
            void buildCacheIfNeeded() const;
            FolderInfo getFolderInfoFromStatement(database::SqliteStatement &stmt) const;

            database::SqliteDatabase &m_db;
        };
    } // namespace database
} // namespace jucyaudio
