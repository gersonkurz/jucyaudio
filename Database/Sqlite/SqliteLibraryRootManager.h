#pragma once

#include <Database/Includes/ILibraryRootManager.h>
#include <Database/Sqlite/SqliteDatabase.h>

namespace jucyaudio
{
    namespace database
    {
        class SqliteLibraryRootManager : public ILibraryRootManager
        {
        public:
            explicit SqliteLibraryRootManager(SqliteDatabase &db);
            ~SqliteLibraryRootManager() override = default;

            std::vector<LibraryRootInfo> getAllRoots() const override;
            std::optional<LibraryRootInfo> addRoot(std::string_view path) override;
            bool updateRootPath(LibraryRootId rootId, std::string_view newPath) override;
            bool removeRoot(LibraryRootId rootId) override;
            bool updateScanStats(LibraryRootId rootId, 
                std::optional<std::chrono::system_clock::time_point> scanTime = std::chrono::system_clock::now()) override;

        private:
            SqliteDatabase &m_db;
        };

    } // namespace database
} // namespace jucyaudio