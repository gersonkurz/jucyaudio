#include <Database/Sqlite/SqliteLibraryRootManager.h>
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
            // Initialize the online status cache on startup
            refreshRootStatuses();
        }

        std::vector<LibraryRootInfo> SqliteLibraryRootManager::getAllRoots() const
        {
            std::vector<LibraryRootInfo> roots;
            SqliteStatement stmt{m_db};
            stmt.query(
                [&roots, &stmt]() -> bool
                {
                    roots.emplace_back(libraryRootInfoFromStatement(stmt));
                    return true;
                },
                "SELECT root_id, path, last_scanned FROM LibraryRoots ORDER BY path;");

            auto &folderDb{theTrackLibrary.getTrackDatabase()->getFolderDatabase()};
            for (auto& root: roots)
            {
                const auto folderId = folderDb.findOrCreateFolderByPath(root.path);
                root.folderInfo = folderDb.getFolderById(folderId).value_or(database::FolderInfo{});
                
                // Set the online status from cache
                auto statusIt = m_onlineStatusCache.find(root.id);
                if (statusIt != m_onlineStatusCache.end())
                {
                    root.isOnline = statusIt->second;
                }
            }

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
            // Clear the cache
            m_onlineStatusCache.clear();
            
            // Also clear the track online cache since root statuses have changed
            theTrackLibrary.clearTrackOnlineCache();
            
            // Load all roots from database
            SqliteStatement stmt{m_db};
            stmt.query(
                [this, &stmt]() -> bool
                {
                    const auto rootId = stmt.getInt64(0);
                    const auto path = stmt.getText(1);
                    
                    // Check if the path exists on the filesystem
                    juce::File rootPath(path);
                    const bool isOnline = rootPath.exists() && rootPath.isDirectory();
                    
                    // Cache the status
                    m_onlineStatusCache[rootId] = isOnline;
                    
                    spdlog::debug("Library root '{}' (id: {}) is {}", 
                                  path, rootId, isOnline ? "online" : "offline");
                    
                    return true;
                },
                "SELECT root_id, path FROM LibraryRoots;");
            
            spdlog::info("Refreshed status for {} library roots", m_onlineStatusCache.size());
        }

        bool SqliteLibraryRootManager::isRootOnline(LibraryRootId rootId) const
        {
            auto it = m_onlineStatusCache.find(rootId);
            if (it != m_onlineStatusCache.end())
            {
                return it->second;
            }
            // Default to offline if not found
            return false;
        }

    } // namespace database
} // namespace jucyaudio