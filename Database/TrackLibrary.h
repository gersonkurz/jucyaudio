#pragma once

#include <Database/Includes/ILongRunningTask.h>
#include <Database/Includes/IMixManager.h>
#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/ITrackDatabase.h>
#include <Database/Includes/IWorkingSetManager.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/Includes/TrackQueryArgs.h>
#include <Database/TrackScanner.h>
#include <filesystem> // For std::filesystem::path
#include <memory>     // For std::unique_ptr
#include <spdlog/spdlog.h>
#include <string>
#include <vector>


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
            /// @brief Returns a retained pointer to the root navigation node.
            /// @return The root navigation node, or nullptr if not initialised.
            INavigationNode *getRootNavigationNode() const;
            // --- Scanning API exposed by TrackLibrary ---
            bool scanLibrary(std::vector<FolderInfo> &foldersToScan, bool forceRescanAllFiles, ProgressCallback progressCb,
                             CompletionCallback completionCb, std::atomic<bool> *shouldCancel);

            const auto &getLastError() const
            {
                return m_lastErrorMessage;
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

            std::optional<TrackInfo> getTrackById(TrackId trackId) const
            {
                if (!m_isInitialised || !m_database)
                {
                    setLastError("TrackLibrary not initialised.");
                    return std::nullopt;
                }
                return m_database->getTrackById(trackId);
            }

            std::vector<TrackInfo> getTracks(const TrackQueryArgs &args) const
            {
                if (!m_isInitialised || !m_database)
                {
                    setLastError("TrackLibrary not initialised.");
                    return std::vector<TrackInfo>{};
                }
                return m_database->getTracks(args);
            }

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
            INavigationNode *const m_rootNavNode;   // Raw pointer
        };

    } // namespace database
} // namespace jucyaudio
