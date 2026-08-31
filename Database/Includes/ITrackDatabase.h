#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/IFolderDatabase.h>
#include <Database/Includes/IMixManager.h>
#include <Database/Includes/ITagManager.h>
#include <Database/Includes/IWorkingSetManager.h>
#include <Database/Includes/IMarkerManager.h>
#include <Database/Includes/IAlbumManager.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/Includes/TrackQueryArgs.h>
#include <Database/Includes/ILibraryRootManager.h>
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
        // Forward declarations
        struct AggregateStats;
        class IMixMarkerManager;
        
        struct AudioMetadata
        {
            float bpm = 0.0f;
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

            /**
             * @brief Writes back only what a scan knows, over a row that already exists.
             *
             * saveTrackInfo's UPDATE writes every column from the TrackInfo it is handed. A scanner
             * builds that TrackInfo from the file, so every column the file cannot answer for is a
             * default - and saving it flattens the BPM and beat analysis, the key, the date the track
             * was added, the album it was sorted into, and the status the format check left. This writes
             * the columns a scan is entitled to write and leaves the rest of the row alone.
             *
             * Genres are added, never removed. A rescan is entitled to notice a genre the file gained;
             * it is not entitled to drop one the user typed into the tag cloud, and the file having no
             * genre frame is not evidence that they did not mean it.
             *
             * @param trackInfo Identifies the row by trackId. Every other field is read as the scan's
             *        answer, so it must have come from a scan - not from a partly-filled struct.
             * @param fields Exactly what the scanners established, from processTrack. Whatever is not
             *        named is left as it is in the database: a field the read did not produce holds a
             *        default, and writing that default erases what the library knew. The location
             *        columns are written regardless - see ScannedFields.
             * @return Failure if no row of that id exists. Not finding the row is the caller having the
             *         wrong id, which is worth hearing about rather than silently doing nothing.
             */
            virtual DbResult updateScannedTrackData(const TrackInfo &trackInfo, ScannedFields fields) = 0;

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
            
            // Overload with progress callback for UI feedback
            using MaintenanceProgressCallback = std::function<void(int percentComplete, const std::string& statusMessage)>;
            virtual bool runMaintenanceTasks(std::atomic<bool> &shouldCancel, MaintenanceProgressCallback progressCb) = 0;

            virtual std::optional<TrackInfo> getTrackById(TrackId trackId) const = 0;
            
            virtual std::vector<TrackInfo> getTracks(const TrackQueryArgs &args) const = 0;
            virtual std::vector<TrackId> getTrackIds(const TrackQueryArgs &args) const = 0;
            virtual int getTotalTrackCount(const TrackQueryArgs &baseFilters) const = 0;

            // Get aggregate statistics for tracks matching the given query
            virtual bool getAggregateStats(const TrackQueryArgs &args, AggregateStats &outStats) const = 0;

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

            /// @brief Update energy analysis data for a single track.
            /// @param trackId The track to update
            /// @param introEnd The calculated intro end timestamp
            /// @param outroStart The calculated outro start timestamp
            /// @param json The JSON string containing the full energy analysis data
            /// @return DbResult indicating success or failure
            virtual DbResult updateTrackEnergyData(TrackId trackId, Duration_t introEnd,
                                                   Duration_t outroStart, const std::string& json) = 0;

            /// @brief Batch update energy analysis data for multiple tracks.
            /// @param results Vector of tuples containing (trackId, introEnd, outroStart, json)
            /// @return DbResult indicating success or failure
            virtual DbResult updateTrackEnergyData(
                const std::vector<std::tuple<TrackId, Duration_t, Duration_t, std::string>>& results) = 0;

            // Used during rescans to update basic file info before deciding on full re-analysis
            virtual DbResult updateTrackFilesystemInfo(TrackId trackId, Timestamp_t lastModified, std::uintmax_t filesize) = 0;

            // To mark a file as no longer found on disk
            virtual DbResult setTrackPathMissing(TrackId trackId, bool isMissing) = 0;
            virtual DbResult removeTracks(const std::vector<TrackId> &trackIds) = 0;

            // @brief Delete tracks from the library (removes from Tracks, MixTracks, WorkingSetTracks)
            // @param trackIds The IDs of the tracks to delete from the library
            // @return DbResult indicating success or failure
            virtual DbResult deleteTracksFromLibrary(const std::vector<TrackId> &trackIds) = 0;

            // Waveform Cache
            virtual DbResult saveWaveform(TrackId trackId, const std::vector<unsigned char>& blob) = 0;
            virtual DbResult loadWaveform(TrackId trackId, std::vector<unsigned char>& blob) = 0;

            virtual IFolderDatabase &getFolderDatabase() const = 0;

            virtual ITagManager &getTagManager() = 0;
            virtual const ITagManager &getTagManager() const = 0;

            virtual IMixManager &getMixManager() = 0;
            virtual const IMixManager &getMixManager() const = 0;

            virtual IWorkingSetManager &getWorkingSetManager() = 0;
            virtual const IWorkingSetManager &getWorkingSetManager() const = 0;
            
            virtual ILibraryRootManager &getLibraryRootManager() = 0;
            virtual const ILibraryRootManager &getLibraryRootManager() const = 0;

            virtual bool getTotalTrackCountForFolders(
                const std::unordered_set<FolderId> &folderIds, int64_t &outCount) const = 0;

            /// @brief Get all folders (including ancestors) that contain tracks matching the search terms.
            /// This is used for folder filtering in the navigation tree to gray out non-matching folders.
            /// @param searchTerms The FTS5 search terms to match against
            /// @return Set of folder IDs that contain matching tracks or are ancestors of such folders
            virtual std::unordered_set<FolderId> getFoldersContainingMatchingTracks(const std::vector<std::string> &searchTerms) const = 0;

            virtual IMarkerManager &getMarkerManager() = 0;
            virtual const IMarkerManager &getMarkerManager() const = 0;
            
            virtual IMixMarkerManager &getMixMarkerManager() = 0;
            virtual const IMixMarkerManager &getMixMarkerManager() const = 0;
            
            virtual IAlbumManager &getAlbumManager() = 0;
            virtual const IAlbumManager &getAlbumManager() const = 0;

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
