#include <chrono> // For filesystem last_modified_time
#include <format>

#include <spdlog/spdlog.h>

#include <Database/Nodes/RootNode.h>
#include <Database/Sqlite/SqliteTrackDatabase.h>
#include <Database/TrackLibrary.h>
#include <Database/TrackScanner.h>


namespace jucyaudio
{
    namespace database
    {
        TrackLibrary::TrackLibrary()
            : m_rootNavNode{new RootNode{*this}}
              // Create a root node with no children
        {
            spdlog::debug("TrackLibrary created.");
        }

        TrackLibrary::~TrackLibrary()
        {
            shutdown();
            if (m_rootNavNode)
            {
                m_rootNavNode->release(
                    REFCOUNT_DEBUG_ARGS); // Release the root node
            }
            spdlog::debug("TrackLibrary destroyed.");
        }

        INavigationNode *TrackLibrary::getRootNavigationNode() const
        {
            if (m_rootNavNode)
            {
                m_rootNavNode->retain(
                    REFCOUNT_DEBUG_ARGS); // Caller gets a new, incremented
                                          // reference.
            }
            return m_rootNavNode;
            // Caller is responsible for calling release() on the pointer they
            // receive.
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

        bool TrackLibrary::scanLibrary(
            std::vector<FolderInfo> &foldersToScan,
            bool forceRescanAllFiles, ProgressCallback progressCb,
            CompletionCallback completionCb, std::atomic<bool> *shouldCancel)
        {
            if (!m_isInitialised || !m_scanner)
            {
                spdlog::error("TrackLibrary not initialised or scanner "
                              "missing, cannot start scan.");
                m_lastErrorMessage = "Library or scanner not initialised.";
                return false;
            }
            return m_scanner->scan(foldersToScan, forceRescanAllFiles,
                                   progressCb, completionCb, shouldCancel);
        }

    } // namespace database
} // namespace jucyaudio
