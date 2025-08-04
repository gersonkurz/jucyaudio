#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/IFolderDatabase.h>
#include <Database/Includes/IMixManager.h>
#include <Database/Includes/ITagManager.h>
#include <Database/Includes/IWorkingSetManager.h>
#include <Database/Includes/IMarkerManager.h>
#include <Database/Includes/IUndoManager.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/Includes/TrackQueryArgs.h>
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
        struct AudioMetadata
        {
            float bpm = 0.0f;
            double introStart = 0.0;
            double introEnd = 0.0;
            double outroStart = 0.0;
            double outroEnd = 0.0;
            bool hasIntro = false;
            bool hasOutro = false;
        };

        struct VirtualFolderInfo
        {
            int64_t folderId = -1;
            int64_t parentId = -1;  // -1 for root folders
            std::string folderName;
            std::string fullPath;
            int depth = 0;
            int directTrackCount = 0;
            int totalTrackCount = 0;
            int64_t directSizeBytes = 0;
            int64_t totalSizeBytes = 0;
        };

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

            // --- Path Reconstruction (NEW) ---
            /**
             * @brief Reconstructs the full, absolute path for a given track.
             * @param trackInfo The track object containing a folderId and filename.
             * @return The full path, or an empty path if reconstruction fails.
             */
            virtual std::filesystem::path reconstructFullPath(const TrackInfo &trackInfo) const = 0;

            /**
             * @brief Reconstructs the full, absolute path for a given folder ID.
             * @param folderId The ID of the folder.
             * @return The full path, or an empty path if reconstruction fails.
             */
            virtual std::filesystem::path reconstructFullPath(FolderId folderId) const = 0;

            virtual bool runMaintenanceTasks(std::atomic<bool> &shouldCancel) = 0; // For maintenance tasks like vacuuming, reindexing, etc.

            virtual std::optional<TrackInfo> getTrackById(TrackId trackId) const = 0;
            
            virtual std::vector<TrackInfo> getTracks(const TrackQueryArgs &args) const = 0;
            virtual std::vector<TrackId> getTrackIds(const TrackQueryArgs &args) const = 0;
            virtual int getTotalTrackCount(const TrackQueryArgs &baseFilters) const = 0;

            // Specific updates, often user-driven or for quick filesystem checks
            virtual DbResult updateTrackRating(TrackId trackId, int rating) = 0;
            virtual DbResult updateTrackLikedStatus(TrackId trackId, int likedStatus) = 0;
            virtual DbResult incrementTrackPlayCount(TrackId trackId) = 0; // And update last_played
            virtual DbResult updateTrackUserNotes(TrackId trackId, const std::string &notes) = 0;
            
            // Update the status of a track (Unknown, Ok, BadFormat)
            virtual DbResult updateTrackStatus(TrackId trackId, TrackStatus status) = 0;

            // Finds one track that has no BPM data and returns its info.
            // Returns std::nullopt if no such tracks are found.
            virtual std::optional<TrackInfo> getNextTrackForBpmAnalysis() const = 0;

            // Performs a targeted update of only the BPM for a given track.
            virtual DbResult updateTrackBpm(TrackId trackId, const AudioMetadata& am) = 0;

            // Performs a batched, transactional update of BPM data for multiple tracks.
            virtual DbResult updateTrackBpm(const std::vector<std::pair<TrackId, AudioMetadata>>& results) = 0;

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
            
            virtual IMarkerManager &getMarkerManager() = 0;
            virtual const IMarkerManager &getMarkerManager() const = 0;
            
            virtual IUndoManager &getUndoManager() = 0;
            virtual const IUndoManager &getUndoManager() const = 0;

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

            /// @brief Build virtual folders from existing tracks in the database.
            /// @details Analyzes all track paths and creates VirtualFolders entries 
            ///          to enable fast folder navigation without filesystem access.
            ///          This is a one-time operation that can be run after initial scan.
            /// @param progressCallback Optional callback for progress updates
            /// @return Result indicating success or failure
            virtual DbResult buildVirtualFolders(
                std::function<void(float /*progress*/, const std::string& /*status*/)> progressCallback = nullptr) = 0;

            // --- Virtual Folder queries ---
            
            /// @brief Get child folders of a parent folder
            /// @param parentId Parent folder ID, or -1 for root folders
            /// @return Vector of child folder information
            virtual std::vector<VirtualFolderInfo> getVirtualFolderChildren(int64_t parentId) const = 0;

            /// @brief Get information about a specific virtual folder
            /// @param folderId The folder ID to query
            /// @return Folder information if found
            virtual std::optional<VirtualFolderInfo> getVirtualFolderInfo(int64_t folderId) const = 0;

            /// @brief Get tracks directly in a virtual folder
            /// @param folderId The folder ID to query
            /// @return Vector of tracks in the folder
            virtual std::vector<TrackInfo> getTracksInVirtualFolder(int64_t folderId) const = 0;

            /// @brief Get the total track count for a virtual folder (including all subfolders)
            /// @param folderId The folder ID to query
            /// @return Total track count including all descendants, or nullopt on error
            virtual std::optional<int64_t> getVirtualFolderTotalTrackCount(int64_t folderId) const = 0;
            
            /// @brief Check if a virtual folder has any subfolders
            /// @param folderId The folder ID to check
            /// @return True if the folder has subfolders
            virtual bool virtualFolderHasChildren(int64_t folderId) const = 0;

        };

    } // namespace database
} // namespace jucyaudio
