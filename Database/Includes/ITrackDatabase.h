#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/TrackQueryArgs.h>
#include <Database/Includes/ITagManager.h>
#include <Database/Includes/IMixManager.h>
#include <Database/Includes/IWorkingSetManager.h>
#include <Database/Includes/IFolderDatabase.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/Includes/MixInfo.h>
#include <chrono>
#include <filesystem>
#include <functional> // For potential callbacks if needed, though not directly for CRUD
#include <optional>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {

        // Simple status for operations, can be expanded
        enum class DbResultStatus
        {
            Ok,
            ErrorGeneric,
            ErrorNotFound,
            ErrorAlreadyExists, // e.g., for unique constraints
            ErrorConstraintFailed,
            ErrorIO,
            ErrorConnection,
            ErrorDB
        };

        struct DbResult
        {
            DbResultStatus status = DbResultStatus::Ok;
            std::string errorMessage;

            DbResult(DbResultStatus s = DbResultStatus::Ok, std::string msg = "")
                : status{s},
                  errorMessage{std::move(msg)}
            {
            }

            bool isOk() const
            {
                return status == DbResultStatus::Ok;
            }
            static DbResult success()
            {
                return DbResult{};
            }
            static DbResult failure(DbResultStatus s, std::string msg)
            {
                return DbResult{s, std::move(msg)};
            }
        };

        
        class ITrackDatabase
        {
        public:
            virtual ~ITrackDatabase() = default;

            // Connection management
            // Connection parameters could be a struct or a variant/map if we need more flexibility later
            virtual DbResult connect(
                const std::filesystem::path &databaseIdentifier) = 0; // For SQLite, this is a file path
                                                                      // For PostgreSQL, this could be a connection string, or we'd overload/use a struct
            virtual void close() = 0;
            virtual bool isOpen() const = 0;
            virtual std::string getLastError() const = 0; // Or last DbResult

            // Schema management
            virtual DbResult createTablesIfNeeded() = 0;
            // virtual int getCurrentSchemaVersion() = 0;
            // virtual DbResult upgradeSchemaTo(int targetVersion) = 0;

            // --- Track CRUD ---
            // Primary method for adding a new track or updating an existing one with full info.
            // If trackInfo.trackId is -1 (or some uninitialized state), it's an INSERT.
            // The method should update trackInfo.trackId with the assigned ID on successful insert.
            // If trackInfo.trackId is valid, it's an UPDATE.
            virtual DbResult saveTrackInfo(TrackInfo &trackInfo) = 0;

            virtual bool runMaintenanceTasks(std::atomic<bool> &shouldCancel) = 0; // For maintenance tasks like vacuuming, reindexing, etc.

            virtual std::optional<TrackInfo> getTrackById(TrackId trackId) const = 0;
            virtual std::optional<TrackInfo> getTrackByFilepath(const std::filesystem::path &filepath) const = 0;

            virtual std::vector<TrackInfo> getTracks(const TrackQueryArgs &args) const = 0;
            virtual int getTotalTrackCount(const TrackQueryArgs &baseFilters) const = 0;

            // Specific updates, often user-driven or for quick filesystem checks
            virtual DbResult updateTrackRating(TrackId trackId, int rating) = 0;
            virtual DbResult updateTrackLikedStatus(TrackId trackId, int likedStatus) = 0;
            virtual DbResult incrementTrackPlayCount(TrackId trackId) = 0; // And update last_played
            virtual DbResult updateTrackUserNotes(TrackId trackId, const std::string &notes) = 0;

            // Used during rescans to update basic file info before deciding on full re-analysis
            virtual DbResult updateTrackFilesystemInfo(TrackId trackId, Timestamp_t lastModified, std::uintmax_t filesize) = 0;

            // To mark a file as no longer found on disk
            virtual DbResult setTrackPathMissing(TrackId trackId, bool isMissing) = 0;
            // (maybe a 'status' field in TrackInfo later)

            virtual IFolderDatabase &getFolderDatabase() const = 0;

            virtual ITagManager &getTagManager() = 0;
            virtual const ITagManager &getTagManager() const = 0;

            virtual IMixManager &getMixManager() = 0;
            virtual const IMixManager &getMixManager() const = 0;

            virtual IWorkingSetManager &getWorkingSetManager() = 0;
            virtual const IWorkingSetManager &getWorkingSetManager() const = 0;



            /// @brief Update the tags for a track.
            /// @param trackId track ID
            /// @param tagIds updated list of tag IDs to associate with the track.
            /// @return result of operation, indicating success or failure.
            virtual DbResult updateTrackTags(TrackId trackId, const std::vector<TagId> &tagIds) = 0;

            /// @brief Get all tags associated with a track.
            /// @param trackId track ID to get tags for.
            /// @return set of tag IDs associated with the track.
            virtual std::vector<TagId> getTrackTags(TrackId trackId) const = 0;

            /// @brief  Get all tags in the database.
            /// @return set of all tag IDs in the database.
            virtual std::vector<TagId> getAllTags() const = 0; // For tag clouds/lists





        };

    } // namespace database
} // namespace jucyaudio
