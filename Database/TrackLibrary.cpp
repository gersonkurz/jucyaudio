#include <chrono> // For filesystem last_modified_time
#include <format>
#include <algorithm>
#include <cctype>

#include <spdlog/spdlog.h>

#include <Database/Nodes/RootNode.h>
#include <Database/Sqlite/SqliteTrackDatabase.h>
#include <Database/TrackLibrary.h>
#include <Database/TrackScanner.h>


namespace jucyaudio
{
    namespace database
    {
        TrackLibrary theTrackLibrary;

        std::filesystem::path TrackInfo::reconstructFullPath() const
        {
            return theTrackLibrary.getTrackDatabase()->reconstructFullPath(*this);
        }

        TrackLibrary::TrackLibrary()
              // Create a root node with no children
        {
            spdlog::debug("TrackLibrary created.");
        }

        TrackLibrary::~TrackLibrary()
        {
            shutdown();
            spdlog::debug("TrackLibrary destroyed.");
        }

        bool TrackLibrary::initialise(
            const std::filesystem::path &databaseFilePath)
        {
            if (m_isInitialised)
            {
                spdlog::warn("TrackLibrary already initialised.");
                return true;
            }

            spdlog::info("Initialising TrackLibrary with database: {}",
                         databaseFilePath.string());

            m_database = new SqliteTrackDatabase{};

            DbResult connectResult = m_database->connect(databaseFilePath);
            if (!connectResult.isOk())
            {
                spdlog::error(
                    "TrackLibrary initialisation failed - DB connect: {}",
                    connectResult.errorMessage);
                delete m_database;
                m_database = nullptr;
                return false;
            }
            m_scanner = new TrackScanner{*m_database}; // Scanner needs the DB

            m_isInitialised = true;
            spdlog::info("TrackLibrary initialised successfully.");
            return true;
        }

        void TrackLibrary::shutdown()
        {
            if (!m_isInitialised)
            {
                return;
            }
            spdlog::info("Shutting down TrackLibrary...");
            if (m_scanner)
            {
                delete m_scanner;
                m_scanner = nullptr;
            }
            if (m_database)
            {
                m_database->close();
                delete m_database;
                m_database = nullptr;
            }
            m_isInitialised = false;
            spdlog::info("TrackLibrary shut down.");
        }

        bool TrackLibrary::scanLibrary(std::vector<FolderId> &foldersToScan,
            bool forceRescanAllFiles,
            bool removeMissingFiles,
            ProgressCallback progressCb,
            CompletionCallback completionCb, std::atomic<bool> *shouldCancel)
        {
            if (!m_isInitialised || !m_scanner)
            {
                spdlog::error("TrackLibrary not initialised or scanner "
                              "missing, cannot start scan.");
                m_lastErrorMessage = "Library or scanner not initialised.";
                return false;
            }
            return m_scanner->scan(foldersToScan, forceRescanAllFiles, removeMissingFiles,
                                   progressCb, completionCb, shouldCancel);
        }

        DbResult TrackLibrary::saveWaveform(TrackId trackId, const std::vector<unsigned char>& blob)
        {
            if (!m_isInitialised || !m_database)
            {
                setLastError("TrackLibrary not initialised.");
                return DbResult::failure(DbResultStatus::ErrorConnection, "Database not initialised.");
            }
            return m_database->saveWaveform(trackId, blob);
        }

        DbResult TrackLibrary::loadWaveform(TrackId trackId, std::vector<unsigned char>& blob)
        {
            if (!m_isInitialised || !m_database)
            {
                setLastError("TrackLibrary not initialised.");
                return DbResult::failure(DbResultStatus::ErrorConnection, "Database not initialised.");
            }
            return m_database->loadWaveform(trackId, blob);
        }

        bool TrackLibrary::isTrackOnline(TrackId trackId) const
        {
            if (!m_isInitialised || !m_database)
            {
                setLastError("TrackLibrary not initialised.");
                return false;
            }

            // Check cache first
            auto cacheIt = m_trackOnlineCache.find(trackId);
            if (cacheIt != m_trackOnlineCache.end())
            {
                spdlog::debug("TrackLibrary::isTrackOnline({}): cache hit -> {}", 
                             trackId, cacheIt->second);
                return cacheIt->second;
            }

            spdlog::info("TrackLibrary::isTrackOnline({}): cache miss, checking...", trackId);

            // Not in cache, need to determine the track's root
            // Get track info to get its folder
            auto trackInfo = m_database->getTrackById(trackId);
            if (!trackInfo.has_value())
            {
                // Track doesn't exist
                spdlog::warn("  Track {} doesn't exist", trackId);
                m_trackOnlineCache[trackId] = false;
                return false;
            }

            // Get the folder's full path
            auto &folderDb = m_database->getFolderDatabase();
            auto folderInfo = folderDb.getFolderById(trackInfo->folderId);
            if (!folderInfo.has_value())
            {
                // Folder doesn't exist
                spdlog::warn("  Folder {} doesn't exist", trackInfo->folderId);
                m_trackOnlineCache[trackId] = false;
                return false;
            }

            // Get the full path of the folder
            const std::string folderPath = folderInfo->path;
            spdlog::info("  Track folder: '{}'", folderPath);

            // Check which library root this folder belongs to
            auto &rootManager = m_database->getLibraryRootManager();
            const auto roots = rootManager.getAllRoots();
            
            spdlog::info("  Checking against {} roots", roots.size());
            
            bool isOnline = false;
            for (const auto &root : roots)
            {
                spdlog::debug("    Comparing folder '{}' with root '{}' (ID {}, online: {})", 
                             folderPath, root.path, root.id, root.isOnline);
                
                // Case-insensitive comparison for macOS/Windows
                std::string folderLower = folderPath;
                std::string rootLower = root.path;
                std::transform(folderLower.begin(), folderLower.end(), folderLower.begin(), ::tolower);
                std::transform(rootLower.begin(), rootLower.end(), rootLower.begin(), ::tolower);
                
                // Check if the folder path starts with this root path
                if (folderLower.find(rootLower) == 0)
                {
                    // This track belongs to this root
                    isOnline = rootManager.isRootOnline(root.id);
                    spdlog::info("  -> Track belongs to root {} ({}), isOnline = {}", 
                                root.id, root.path, isOnline);
                    break;
                }
            }

            if (!isOnline)
            {
                spdlog::info("  -> Track doesn't belong to any online root");
            }

            // Cache the result
            m_trackOnlineCache[trackId] = isOnline;
            return isOnline;
        }

        void TrackLibrary::clearTrackOnlineCache() const
        {
            m_trackOnlineCache.clear();
            spdlog::debug("Cleared track online status cache");
        }

    } // namespace database
} // namespace jucyaudio
