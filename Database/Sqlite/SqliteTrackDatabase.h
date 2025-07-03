#pragma once

#include <Database/Includes/ITrackDatabase.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTagManager.h>
#include <Database/Sqlite/SqliteMixManager.h>
#include <Database/Sqlite/SqliteFolderDatabase.h>
#include <Database/Sqlite/SqliteWorkingSetManager.h>
#include <Database/Sqlite/sqlite3.h>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {

        class SqliteTrackDatabase : public ITrackDatabase
        {
        public:
            SqliteTrackDatabase();
            ~SqliteTrackDatabase() override; // Important to override virtual destructor

            // Non-copyable, non-movable because of the raw sqlite3* handle
            SqliteTrackDatabase(const SqliteTrackDatabase &) = delete;
            SqliteTrackDatabase &operator=(const SqliteTrackDatabase &) = delete;
            SqliteTrackDatabase(SqliteTrackDatabase &&) = delete;
            SqliteTrackDatabase &operator=(SqliteTrackDatabase &&) = delete;

            // --- ITrackDatabase Interface Implementation ---
            DbResult connect(const std::filesystem::path &databaseFilePath) override;
            void close() override;
            bool isOpen() const override;
            std::string getLastError() const override; // Gets from lastErrorMessage
            bool runMaintenanceTasks(std::atomic<bool> &shouldCancel) override;
            DbResult createTablesIfNeeded() override;
            // int getCurrentSchemaVersion() override; // Implementation for schema versioning
            // DbResult upgradeSchemaTo(int targetVersion) override;

            // Track CRUD
            DbResult saveTrackInfo(TrackInfo &trackInfo) override;
            std::optional<TrackInfo> getTrackById(TrackId trackId) const override;
            std::optional<TrackInfo> getTrackByFilepath(const std::filesystem::path &filepath) const override;

            std::vector<TrackInfo> getTracks(const TrackQueryArgs &args) const override;
            int getTotalTrackCount(const TrackQueryArgs &baseFilters) const override;
            int getTotalTrackCount() const // Without filters, but with cache
            {
                if (!m_cachedTotalTrackCountValid)
                {
                    m_cachedTotalTrackCount = getTotalTrackCount(TrackQueryArgs{});
                    m_cachedTotalTrackCountValid = true;
                }
                return m_cachedTotalTrackCount;
            }

            DbResult updateTrackRating(TrackId trackId, int rating) override;
            DbResult updateTrackLikedStatus(TrackId trackId, int likedStatus) override;
            DbResult incrementTrackPlayCount(TrackId trackId) override;
            DbResult updateTrackUserNotes(TrackId trackId, const std::string &notes) override;
            DbResult updateTrackFilesystemInfo(TrackId trackId, Timestamp_t lastModified, std::uintmax_t filesize) override;
            DbResult setTrackPathMissing(TrackId trackId, bool isMissing) override;

            IFolderDatabase &getFolderDatabase() const override;


            ITagManager &getTagManager() override;
            const ITagManager &getTagManager() const override;

            IMixManager &getMixManager() override;
            const IMixManager &getMixManager() const override;

            IWorkingSetManager &getWorkingSetManager() override;
            const IWorkingSetManager &getWorkingSetManager() const override;

            DbResult updateTrackTags(TrackId trackId, const std::vector<TagId>& tagIds) override;
            std::vector<TagId> getTrackTags(TrackId trackId) const override;
            std::vector<TagId> getAllTags() const override;


        private:
            template <typename T>
            DbResult updateSingleTrackField(TrackId trackId, const std::string &columnName, T value,
                                            std::function<bool(database::SqliteStatement &, T)> binder);
            void readAllTagTracks(std::vector<TrackInfo> &tracks) const;
            bool updateTrackTagsFromInsideTransaction(TrackId trackId, const std::vector<TagId>& tagIds);
        private:
            mutable database::SqliteDatabase m_db;
            mutable SqliteTagManager m_tagManager;
            mutable SqliteMixManager m_mixManager;
            mutable SqliteWorkingSetManager m_workingSetManager; // Working set manager instance
            mutable SqliteFolderDatabase m_folderDatabase; // Folder database instance
            std::filesystem::path m_databaseFilePath; // Store the path
            mutable std::string m_lastErrorMessage;   // For getLastError()

            mutable int m_cachedTotalTrackCount{0}; // Cache for total track count
            mutable bool m_cachedTotalTrackCountValid{false}; // Flag to check if cache is valid

            // Schema versioning helpers
            int getDBSchemaVersion();
            DbResult setDBSchemaVersion(int version);
            DbResult runMigrations(); // Calls specific migration steps
        };

    } // namespace database
} // namespace jucyaudio
