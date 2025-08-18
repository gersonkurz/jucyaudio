#include <Database/Sqlite/SqliteLibraryRootManager.h>
#include <Database/Sqlite/SqliteFolderDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>
#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>

namespace
{
    // Helper to construct a LibraryRootInfo object from a query result.
    jucyaudio::database::LibraryRootInfo libraryRootInfoFromStatement(const jucyaudio::database::SqliteStatement &stmt)
    {
        jucyaudio::database::LibraryRootInfo info{};
        info.id = stmt.getInt64(0);
        info.path = stmt.getText(1);
        if (!stmt.isNull(2))
        {
            const auto timestamp = stmt.getInt64(2);
            info.lastScanned = std::chrono::system_clock::from_time_t(timestamp);
        }
        return info;
    }
} // namespace

namespace jucyaudio
{
    namespace database
    {

        SqliteLibraryRootManager::SqliteLibraryRootManager(SqliteDatabase &db)
            : m_db{db}
        {
            // Don't call refreshRootStatuses here - the database might not be ready yet
            // It will be called later when needed
        }

        std::vector<LibraryRootInfo> SqliteLibraryRootManager::getAllRoots() const
        {
            spdlog::info("=== getAllRoots called ===");
            
            // Don't auto-refresh during initialization - let the caller handle it
            // This avoids circular dependencies during TrackLibrary initialization
            
            std::vector<LibraryRootInfo> roots;
            SqliteStatement stmt{m_db};
            stmt.query(
                [&roots, &stmt]() -> bool
                {
                    roots.emplace_back(libraryRootInfoFromStatement(stmt));
                    return true;
                },
                "SELECT root_id, path, last_scanned FROM LibraryRoots ORDER BY path;");

            // Don't try to get folder info here - it causes circular dependency during initialization
            for (auto& root: roots)
            {
                // folderInfo will be populated later when needed
                
                // Set the online status from cache
                auto statusIt = m_onlineStatusCache.find(root.id);
                if (statusIt != m_onlineStatusCache.end())
                {
                    root.isOnline = statusIt->second;
                    spdlog::info("  Root ID {} ({}): isOnline set to {} from cache", 
                                root.id, root.path, root.isOnline);
                }
                else
                {
                    spdlog::warn("  Root ID {} ({}): NOT in cache, isOnline remains {}", 
                                root.id, root.path, root.isOnline);
                }
            }

            spdlog::info("=== getAllRoots returning {} roots ===", roots.size());
            return roots;
        }

        std::optional<LibraryRootInfo> SqliteLibraryRootManager::addRoot(std::string_view path)
        {
            // First, check if a root with this path already exists to avoid UNIQUE constraint errors.
            SqliteStatement checkStmt{m_db, "SELECT COUNT(*) FROM LibraryRoots WHERE path = ?;"};
            checkStmt.addParam(path);
            if (checkStmt.getNextResult() && checkStmt.getInt64(0) > 0)
            {
                spdlog::warn("Library root '{}' already exists.", path);
                return std::nullopt;
            }

            SqliteTransaction transaction{m_db};
            if (transaction.execute("INSERT INTO LibraryRoots (path) VALUES (?);", path))
            {
                LibraryRootInfo newRoot;
                newRoot.id = m_db.getLastInsertRowId();
                newRoot.path = path;
                // lastScanned remains std::nullopt for new roots
                if (transaction.commit())
                {
                    spdlog::info("Added library root '{}' with id {}.", path, newRoot.id);
                    return newRoot;
                }
            }
            return std::nullopt;
        }

        bool SqliteLibraryRootManager::updateRootPath(LibraryRootId rootId, std::string_view newPath)
        {
            // NOTE: This is a 'dumb' rename for now. It only updates this table.
            // A 'smart' rename that handles re-parenting the folder hierarchy
            // is a more complex operation that we need to build on top of this.
            SqliteTransaction transaction{m_db};
            if (transaction.execute("UPDATE LibraryRoots SET path = ? WHERE root_id = ?;", newPath, rootId))
            {
                return transaction.commit();
            }
            return false;
        }

        bool SqliteLibraryRootManager::removeRoot(LibraryRootId rootId)
        {
            SqliteTransaction transaction{m_db};
            if (transaction.execute("DELETE FROM LibraryRoots WHERE root_id = ?;", rootId))
            {
                return transaction.commit();
            }
            return false;
        }

        bool SqliteLibraryRootManager::updateScanStats(LibraryRootId rootId, 
            std::optional<Timestamp_t> scanTime)
        {
            SqliteTransaction transaction{m_db};
            
            const Timestamp_t timestamp = scanTime.has_value() 
                ? scanTime.value()
                : std::chrono::system_clock::now();
            
            if (transaction.execute("UPDATE LibraryRoots SET last_scanned = ? WHERE root_id = ?;", 
                                    timestampToInt64(timestamp), rootId))
            {
                if (transaction.commit())
                {
                    spdlog::info("Updated library root {}", rootId);
                    return true;
                }
            }
            return false;
        }

        void SqliteLibraryRootManager::refreshRootStatuses()
        {
            spdlog::info("=== Starting refreshRootStatuses ===");
            
            // Clear the cache
            m_onlineStatusCache.clear();
            
            // Also clear the track online cache since root statuses have changed
            // But only if the TrackLibrary is initialized
            if (theTrackLibrary.isInitialised())
            {
                theTrackLibrary.clearTrackOnlineCache();
            }
            
            // Load all roots from database
            SqliteStatement stmt{m_db};
            stmt.query(
                [this, &stmt]() -> bool
                {
                    const auto rootId = stmt.getInt64(0);
                    const auto path = stmt.getText(1);
                    
                    spdlog::info("Checking root ID {} with path: '{}'", rootId, path);
                    
                    // Check if the path exists on the filesystem
                    juce::File rootPath(path);
                    const bool exists = rootPath.exists();
                    const bool isDir = rootPath.isDirectory();
                    const bool isOnline = exists && isDir;
                    
                    // Cache the status
                    m_onlineStatusCache[rootId] = isOnline;
                    
                    spdlog::info("  -> exists: {}, isDirectory: {}, isOnline: {}", 
                                  exists, isDir, isOnline);
                    
                    // Additional debug info
                    if (exists)
                    {
                        spdlog::info("  -> Full path: {}", rootPath.getFullPathName().toStdString());
                        spdlog::info("  -> Parent exists: {}", rootPath.getParentDirectory().exists());
                    }
                    
                    return true;
                },
                "SELECT root_id, path FROM LibraryRoots;");
            
            spdlog::info("=== Completed refreshRootStatuses: {} roots cached ===", m_onlineStatusCache.size());
            
            // Log the final cache state
            for (const auto& [id, online] : m_onlineStatusCache)
            {
                spdlog::info("  Cache: Root ID {} -> {}", id, online ? "ONLINE" : "OFFLINE");
            }
            
            // Don't call rebuildOfflineFoldersTable here - it creates a circular dependency
            // It will be called separately after initialization
        }

        bool SqliteLibraryRootManager::isRootOnline(LibraryRootId rootId) const
        {
            auto it = m_onlineStatusCache.find(rootId);
            if (it != m_onlineStatusCache.end())
            {
                spdlog::debug("isRootOnline: Root ID {} found in cache -> {}", 
                             rootId, it->second ? "ONLINE" : "OFFLINE");
                return it->second;
            }
            // Default to offline if not found
            spdlog::warn("isRootOnline: Root ID {} NOT found in cache, defaulting to OFFLINE", rootId);
            return false;
        }

    } // namespace database
} // namespace jucyaudio