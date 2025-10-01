#pragma once

#include <Database/Includes/ILongRunningTask.h>
#include <Database/Includes/IMixManager.h>
#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/ITrackDatabase.h>
#include <Database/Includes/IWorkingSetManager.h>
#include <Database/Includes/IMarkerManager.h>
#include <Database/Includes/IAlbumManager.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/Includes/TrackQueryArgs.h>
#include <Database/TrackScanner.h>
#include <filesystem> // For std::filesystem::path
#include <memory>     // For std::unique_ptr
#include <spdlog/spdlog.h>
#include <string>
#include <vector>
#include <map>


// Forward declare Juce types if we use them for callbacks to UI,
// but the core engine functions won't take/return them directly.
// For now, let's keep it Juce-free in this header for the engine part.
// Callbacks will be std::function.

namespace jucyaudio
{
    namespace database
    {
        class TrackLibrary final
        {
        public:
            TrackLibrary();
            ~TrackLibrary();

            // Non-copyable
            TrackLibrary(const TrackLibrary &) = delete;
            TrackLibrary &operator=(const TrackLibrary &) = delete;
            // Movable (if needed, though for a central engine, maybe not)
            TrackLibrary(TrackLibrary &&) = delete;
            TrackLibrary &operator=(TrackLibrary &&) = delete;

            // Initialization
            // Takes the path where the SQLite database file should be located/created.
            // Returns true on success, false on failure.
            bool initialise(const std::filesystem::path &databaseFilePath);
            void shutdown(); // Closes DB, cleans up resources
            bool isInitialised() const
            {
                return m_isInitialised;
            }

            // --- Scanning API exposed by TrackLibrary ---
            bool scanLibrary(std::vector<FolderId> &foldersToScan,
                bool forceRescanAllFiles,
                bool removeMissingFiles,
                ProgressCallback progressCb,
                             CompletionCallback completionCb, std::atomic<bool> *shouldCancel);

            const auto &getLastError() const
            {
                return m_lastErrorMessage;
            }

            ITrackDatabase *getTrackDatabase() const
            {
                if (!m_isInitialised || !m_database)
                {
                    setLastError("TrackLibrary not initialised.");
                    return nullptr;
                }
                return m_database;
            }

            ITagManager *getTagManager()
            {
                if (!m_isInitialised || !m_database)
                {
                    setLastError("TrackLibrary not initialised.");
                    return nullptr;
                }
                return &m_database->getTagManager();
            }

            const IMixManager &getMixManager() const
            {
                return m_database->getMixManager();
            }

            IFolderDatabase &getFolderDatabase() const
            {
                // why not check for database? Because if you ever get here, whole application state is broken anyway
                // and references are preferable to avoid repeating null checks.
                return m_database->getFolderDatabase();
            }

            IWorkingSetManager &getWorkingSetManager() const
            {
                return m_database->getWorkingSetManager();
            }
            
            IMarkerManager &getMarkerManager() const
            {
                return m_database->getMarkerManager();
            }

            IAlbumManager &getAlbumManager() const
            {
                return m_database->getAlbumManager();
            }

            ILibraryRootManager& getLibraryRootManager()
            {
                return m_database->getLibraryRootManager();
            }

            const ILibraryRootManager &getLibraryRootManager() const
            {
                return m_database->getLibraryRootManager();
            }

            
            int getTotalTrackCount(const TrackQueryArgs &baseFilters = TrackQueryArgs{}) const
            {
                if (!m_isInitialised || !m_database)
                {
                    setLastError("TrackLibrary not initialised.");
                    return 0;
                }
                return m_database->getTotalTrackCount(baseFilters);
            }

            bool runMaintenanceTasks(std::atomic<bool> &shouldCancel)
            {
                if (!m_isInitialised || !m_database)
                {
                    setLastError("TrackLibrary not initialised.");
                    return false;
                }
                return m_database->runMaintenanceTasks(shouldCancel);
            }
            
            bool runMaintenanceTasks(std::atomic<bool> &shouldCancel, ITrackDatabase::MaintenanceProgressCallback progressCb)
            {
                if (!m_isInitialised || !m_database)
                {
                    setLastError("TrackLibrary not initialised.");
                    return false;
                }
                return m_database->runMaintenanceTasks(shouldCancel, progressCb);
            }

            std::optional<TrackInfo> getTrackById(TrackId trackId) const
            {
                if (!m_isInitialised || !m_database)
                {
                    setLastError("TrackLibrary not initialised.");
                    return std::nullopt;
                }
                return m_database->getTrackById(trackId);
            }

            // Waveform Cache
            DbResult saveWaveform(TrackId trackId, const std::vector<unsigned char>& blob);
            DbResult loadWaveform(TrackId trackId, std::vector<unsigned char> &blob);

            std::vector<TrackInfo> getTracks(const TrackQueryArgs &args) const
            {
                if (!m_isInitialised || !m_database)
                {
                    setLastError("TrackLibrary not initialised.");
                    return std::vector<TrackInfo>{};
                }
                return m_database->getTracks(args);
            }

            /**
             * @brief Checks if a track is available (its library root is online).
             * Uses caching to avoid repeated database lookups.
             * @param trackId The ID of the track to check.
             * @return true if the track's library root is online, false otherwise.
             */
            bool isTrackOnline(TrackId trackId) const;

            /**
             * @brief Clears the track-to-root cache. Should be called when
             * library roots are refreshed.
             */
            void clearTrackOnlineCache() const;

        private:
            bool setLastError(std::string_view errorMessage) const
            {
                m_lastErrorMessage = errorMessage;
                spdlog::error("TrackLibrary Error: {}", errorMessage);
                return false; // For consistency, return false on error
            }

        private:
            ITrackDatabase *m_database{nullptr};
            TrackScanner *m_scanner{nullptr};
            bool m_isInitialised{false};
            mutable std::string m_lastErrorMessage; // For getLastError()
            
            // Cache for track online status (trackId -> isOnline)
            mutable std::map<TrackId, bool> m_trackOnlineCache;
        };

        extern TrackLibrary theTrackLibrary;
    } // namespace database
} // namespace jucyaudio
