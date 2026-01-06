#include <Database/Includes/AlbumInfo.h>
#include <Database/Includes/ILibraryRootManager.h>
#include <Database/Sqlite/SqliteFolderDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Utils/AssortedUtils.h> // For pathToString etc.
#include <algorithm>
#include <cassert>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        SqliteFolderDatabase::SqliteFolderDatabase(SqliteDatabase &db)
            : m_db{db}
        {
        }

        bool SqliteFolderDatabase::buildCacheIfNeeded() const
        {
            std::lock_guard lock{m_cacheMutex};
            if (m_isCacheValid)
                return true;

            spdlog::debug("Building all folder caches...");

            m_folderInfoFromId.clear();
            m_idFromFolderPath.clear();
            m_childrenFromParents.clear();
            m_parentsFromChildren.clear();

            auto reserveFromCount = [this](const char *sql, auto &container)
            {
                SqliteStatement countStmt{m_db, sql};
                if (countStmt.isValid() && countStmt.getNextResult())
                {
                    const auto count = static_cast<size_t>(countStmt.getInt64(0));
                    if (count > 0)
                    {
                        container.reserve(count);
                    }
                }
            };

            reserveFromCount("SELECT COUNT(*) FROM Folders", m_folderInfoFromId);
            reserveFromCount("SELECT COUNT(*) FROM Folders", m_idFromFolderPath);
            reserveFromCount("SELECT COUNT(*) FROM Folders", m_childrenFromParents);
            reserveFromCount("SELECT COUNT(*) FROM Folders", m_parentsFromChildren);

            SqliteStatement stmt{m_db,
                                 "SELECT folder_id, COALESCE(parent_id, -1), name, "
                                 "COALESCE(root_path, ''), COALESCE(actual_path, '') FROM Folders;"};
            if (!stmt.isValid())
            {
                spdlog::error("buildCacheIfNeeded: Failed to prepare statement. DB error: {}", m_db.getLastError());
                return false;
            }

            spdlog::info("buildCacheIfNeeded: Fetching all folders from the database...");

            std::vector<FolderId> roots;
            FolderInfo info{};
            while (stmt.getNextResult())
            {
                info.folderId = stmt.getInt64(0);
                info.parentId = stmt.getInt64(1);
                const char *nameRaw = stmt.getTextRaw(2);
                const char *pathRaw = stmt.getTextRaw(3);
                const char *actualPathRaw = stmt.getTextRaw(4);
                info.name = nameRaw ? nameRaw : "";
                info.path = pathRaw ? pathRaw : "";
                info.actualPath = actualPathRaw ? actualPathRaw : "";
                info.trackCount = 0;

                m_folderInfoFromId[info.folderId] = info;
                if (info.parentId > 0)
                {
                    registerAsParent(info.parentId, info.folderId);
                }
                else
                {
                    roots.push_back(info.folderId);
                }
            }

            // lookup list for updates of the root_path value
            std::unordered_map<FolderId, std::string> pathUpdates;

            std::vector<FolderId> currentLevel = std::move(roots);
            std::vector<FolderId> nextLevel;

            for (const auto rootId : currentLevel)
            {
                m_parentsFromChildren[rootId] = {rootId};
            }

            size_t visitedCount = 0;
            while (!currentLevel.empty())
            {
                nextLevel.clear();

                for (const auto folderId : currentLevel)
                {
                    auto it = m_folderInfoFromId.find(folderId);
                    if (it == m_folderInfoFromId.end())
                        continue;
                    ++visitedCount;

                    auto &folderInfo = it->second;
                    if (folderInfo.path.empty())
                    {
                        if (folderInfo.parentId <= 0)
                        {
                            folderInfo.path = normalizeForCache(folderInfo.name);
                        }
                        else
                        {
                            auto parentIt = m_folderInfoFromId.find(folderInfo.parentId);
                            if (parentIt != m_folderInfoFromId.end())
                            {
                                folderInfo.path = normalizeForCache(parentIt->second.path + "\\" + folderInfo.name);
                            }
                            else
                            {
                                spdlog::error("buildCacheIfNeeded: Missing parent {} for folder {}", folderInfo.parentId, folderId);
                                return false;
                            }
                        }
                        pathUpdates[folderId] = folderInfo.path;
                    }

                    const auto pf{m_idFromFolderPath.find(folderInfo.path)};
                    if (pf != m_idFromFolderPath.end())
                    {
                        spdlog::error("Found duplicate: Folder {} already exists in m_folderInfoFromId", folderInfo.path);
                        spdlog::error("New ID is {}", folderInfo.folderId);
                        spdlog::error("Existing ID is {}", pf->second);
                        return false;
                    }
                    m_idFromFolderPath[folderInfo.path] = folderInfo.folderId;

                    if (folderInfo.parentId > 0 && !m_parentsFromChildren.contains(folderId))
                    {
                        auto parentChainIt = m_parentsFromChildren.find(folderInfo.parentId);
                        if (parentChainIt == m_parentsFromChildren.end())
                        {
                            spdlog::error("buildCacheIfNeeded: Missing parent chain for {} (parent {})", folderId, folderInfo.parentId);
                            return false;
                        }
                        std::vector<FolderId> chain;
                        chain.reserve(parentChainIt->second.size() + 1);
                        chain.push_back(folderId);
                        chain.insert(chain.end(), parentChainIt->second.begin(), parentChainIt->second.end());
                        m_parentsFromChildren[folderId] = std::move(chain);
                    }

                    auto childrenIt = m_childrenFromParents.find(folderId);
                    if (childrenIt != m_childrenFromParents.end())
                    {
                        for (const auto childId : childrenIt->second)
                        {
                            nextLevel.push_back(childId);
                        }
                    }
                }

                std::swap(currentLevel, nextLevel);
            }
            if (visitedCount != m_folderInfoFromId.size())
            {
                spdlog::error("buildCacheIfNeeded: visited {} folders, expected {}. Orphaned folders likely exist.",
                              visitedCount, m_folderInfoFromId.size());
                return false;
            }

            spdlog::info("buildCacheIfNeeded: complete with {} folders loaded.", m_folderInfoFromId.size());

            if (!pathUpdates.empty())
            {
                updateRootPathValuesInDatabase(pathUpdates);
            }
            spdlog::info("BEGIN recursive track count calculation");

            struct ExistingAlbumInfo
            {
                AlbumId albumId{0};
                std::string albumArtist;
                std::string title;
            };

            struct NewAlbumInfo
            {
                std::string albumArtist;
                std::string title;
                FolderId folderId{0};
            };

            // build lookup map of existing albums by folder ID
            std::unordered_map<FolderId, ExistingAlbumInfo> albumsByFolder;
            reserveFromCount("SELECT COUNT(*) FROM Albums", albumsByFolder);

            SqliteStatement albumQuery{m_db, "SELECT album_id, album_artist, title, folder_id FROM Albums"};
            while (albumQuery.getNextResult())
            {
                ExistingAlbumInfo albumInfo;
                albumInfo.albumId = albumQuery.getInt64(0);
                albumInfo.albumArtist = albumQuery.getText(1);
                albumInfo.title = albumQuery.getText(2);
                const FolderId folderId = albumQuery.getInt64(3);
                assert(albumInfo.albumId >= 0 && "Album ID should be non-negative");
                assert(!albumsByFolder.contains(folderId) && "Folder should not have multiple albums in this context");

                albumsByFolder[folderId] = std::move(albumInfo);
            }

            FolderId lastKnownFolderId = -1;
            std::string lastKnownArtistName;
            std::string lastKnownAlbumName;
            bool useThisFolder = true;
            bool folderAlreadyHasAlbum = false;

            std::vector<NewAlbumInfo> albums;
            std::unordered_set<FolderId> albumFolders;
            reserveFromCount("SELECT COUNT(*) FROM Tracks", albumFolders);

            SqliteStatement countStmt{m_db, "SELECT folder_id, COALESCE(artist_name, ''), COALESCE(album_title, '') FROM Tracks ORDER BY folder_ID ASC"};
            while (countStmt.getNextResult())
            {
                const FolderId folderId = countStmt.getInt64(0);
                const char *artistRaw = countStmt.getTextRaw(1);
                const char *albumRaw = countStmt.getTextRaw(2);
                const std::string_view artistName{artistRaw ? artistRaw : ""};
                const std::string_view albumName{albumRaw ? albumRaw : ""};

                if (folderId != lastKnownFolderId)
                {
                    if (useThisFolder)
                    {
                        if (!folderAlreadyHasAlbum && (lastKnownFolderId > 0))
                        {
                            if (albumFolders.contains(lastKnownFolderId))
                            {
                                spdlog::warn("Folder {} already has an album, skipping for folder ID {}", lastKnownFolderId, lastKnownFolderId);
                            }
                            else if (!lastKnownArtistName.empty())
                            {
                                albumFolders.insert(lastKnownFolderId);

                                NewAlbumInfo nai;
                                nai.albumArtist = lastKnownArtistName;
                                nai.title = lastKnownAlbumName;
                                nai.folderId = lastKnownFolderId;
                                albums.push_back(nai);
                            }
                        }
                    }
                    lastKnownArtistName.clear();
                    lastKnownAlbumName.clear();
                    useThisFolder = true;
                    folderAlreadyHasAlbum = false;
                    lastKnownFolderId = folderId;
                }
                else if (useThisFolder)
                {
                    if (artistName.empty())
                    {
                        useThisFolder = false; // Skip this folder for album creation
                    }
                    else if (albumName.empty())
                    {
                        useThisFolder = false; // Skip this folder for album creation
                    }
                    else if (lastKnownArtistName.empty())
                    {
                        auto item = albumsByFolder.find(folderId);
                        if (item != albumsByFolder.end())
                        {
                            if (item->second.albumArtist != artistName || item->second.title != albumName)
                            {
                                useThisFolder = false; // Skip this folder for album creation
                            }
                            else
                            {
                                folderAlreadyHasAlbum = true; // This folder already has an album
                            }
                        }
                        if (useThisFolder)
                        {
                            lastKnownArtistName = artistName;
                            lastKnownAlbumName = albumName;
                        }
                    }
                    else if (lastKnownArtistName != artistName || lastKnownAlbumName != albumName)
                    {
                        lastKnownArtistName.clear();
                        lastKnownAlbumName.clear();
                        useThisFolder = false; // Skip this folder for album creation
                    }
                }

                auto item = m_parentsFromChildren.find(folderId);
                if (item != m_parentsFromChildren.end())
                {
                    for (const auto parentId : item->second)
                    {
                        auto it = m_folderInfoFromId.find(parentId);
                        if (it != m_folderInfoFromId.end())
                        {
                            ++(it->second.trackCount);
                        }
                    }
                }
            }
            if (useThisFolder && !folderAlreadyHasAlbum && lastKnownFolderId > 0)
            {
                if (!albumFolders.contains(lastKnownFolderId) &&
                    !lastKnownArtistName.empty() && !lastKnownAlbumName.empty())
                {
                    NewAlbumInfo nai;
                    nai.albumArtist = lastKnownArtistName;
                    nai.title = lastKnownAlbumName;
                    nai.folderId = lastKnownFolderId;
                    albums.push_back(nai);
                }
            }
            spdlog::info("FINISHED recursive track count calculation for {:L} folders and {:L} new albums", m_folderInfoFromId.size(), albums.size());

            if (!albums.empty())
            {
                if (SqliteTransaction transaction{m_db})
                {
                    SqliteStatement stmt{m_db, "INSERT INTO Albums (album_artist, title, folder_id) VALUES (?, ?, ?)"};
                    for (const auto &item : albums)
                    {
                        stmt.addParam(item.albumArtist);
                        stmt.addParam(item.title);
                        stmt.addParam(item.folderId);
                        if (!stmt.execute())
                        {
                            spdlog::error("Failed to update folder with ID: {}", item.folderId);
                            return transaction.rollback();
                        }
                        stmt.reset();
                    }
                    transaction.commit();
                }
            }

            m_isCacheValid = true;
            return true;
        }

        void SqliteFolderDatabase::getChildFoldersRecursive(std::unordered_set<FolderId> &allChildIds, FolderId folderId) const
        {
            allChildIds.insert(folderId);

            // Check if we have children for this folder in our cache
            const auto it = m_childrenFromParents.find(folderId);
            if (it != m_childrenFromParents.end())
            {
                // and now, add their children recursively
                for (const auto childId : it->second)
                {
                    getChildFoldersRecursive(allChildIds, childId);
                }
            }
        }

        std::unordered_set<FolderId> SqliteFolderDatabase::getAllChildFolders(const std::vector<FolderId> &folderIdsToScan) const
        {
            buildCacheIfNeeded();

            std::unordered_set<FolderId> allChildIds;
            for (const auto &folderId : folderIdsToScan)
            {
                getChildFoldersRecursive(allChildIds, folderId);
            }
            return allChildIds;
        }

        bool SqliteFolderDatabase::updateRootPathValuesInDatabase(const std::unordered_map<FolderId, std::string> &pathUpdates) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                SqliteStatement stmt{m_db, "UPDATE Folders SET root_path=? WHERE folder_id = ?"};

                for (const auto &item : pathUpdates)
                {
                    stmt.addParam(item.second);
                    stmt.addParam(item.first);
                    if (!stmt.execute())
                    {
                        spdlog::error("Failed to update folder with ID: {}", item.first);
                        return transaction.rollback();
                    }
                    stmt.reset();
                }

                return transaction.commit();
            }
            spdlog::error("updateRootPathValuesInDatabase: Failed to start transaction.");
            return false;
        }

        void SqliteFolderDatabase::invalidateCache() const
        {
            std::lock_guard lock(m_cacheMutex);
            m_isCacheValid = false;
            m_folderInfoFromId.clear();
            m_idFromFolderPath.clear();
            m_childrenFromParents.clear();
            spdlog::debug("Folder cache invalidated.");
        }

        std::optional<FolderInfo> SqliteFolderDatabase::getFolderById(FolderId folderId) const
        {
            buildCacheIfNeeded();
            std::lock_guard lock{m_cacheMutex};

            const auto it = m_folderInfoFromId.find(folderId);
            if (it != m_folderInfoFromId.end())
            {
                return it->second;
            }
            return std::nullopt;
        }

        bool SqliteFolderDatabase::hasChildren(FolderId parentId) const
        {
            buildCacheIfNeeded();
            std::lock_guard lock{m_cacheMutex};

            // Check if parent has any children in the map
            auto it = m_childrenFromParents.find(parentId);
            if (it == m_childrenFromParents.end())
                return false;

            // Check if any of the children have valid (non-empty) names
            for (const auto childId : it->second)
            {
                auto folderIt = m_folderInfoFromId.find(childId);
                if (folderIt != m_folderInfoFromId.end() && !folderIt->second.name.empty())
                {
                    return true;  // Found at least one valid child
                }
            }

            return false;  // No valid children found
        }


        std::unordered_set<FolderId> SqliteFolderDatabase::getParentSet(FolderId folderId) const
        {
            buildCacheIfNeeded();
            std::lock_guard lock(m_cacheMutex);

            std::unordered_set<FolderId> parents;
            while (folderId > 0)
            {
                parents.insert(folderId);
                auto it = m_folderInfoFromId.find(folderId);
                if (it == m_folderInfoFromId.end())
                    break;
                folderId = it->second.parentId;
            }
            return parents;
        }

        std::vector<FolderInfo> SqliteFolderDatabase::getChildFolders(FolderId parentId) const
        {
            buildCacheIfNeeded();
            std::lock_guard lock(m_cacheMutex);

            std::vector<FolderInfo> children;

            // 1. Perform a fast lookup in our child index cache.
            auto it = m_childrenFromParents.find(parentId);
            if (it != m_childrenFromParents.end())
            {
                const auto &childIds = it->second;
                children.reserve(childIds.size());

                // 2. For each child ID, get the full FolderInfo from the main cache.
                for (const auto childId : childIds)
                {
                    auto folderIt = m_folderInfoFromId.find(childId);
                    if (folderIt != m_folderInfoFromId.end())
                    {
                        // Skip folders with empty names (data corruption or scanning issue)
                        if (!folderIt->second.name.empty())
                        {
                            children.push_back(folderIt->second);
                        }
                        else
                        {
                            spdlog::warn("Skipping folder ID {} with empty name (parent ID: {})", childId, parentId);
                        }
                    }
                }
            }

            // 3. Sort the final list for consistent UI presentation.
            std::sort(children.begin(),
                children.end(),
                [](const FolderInfo &a, const FolderInfo &b)
                {
                    auto normA = normalizeForCache(a.name);
                    auto normB = normalizeForCache(b.name);
                    return normA < normB;
                });

            return children;
        }

        bool SqliteFolderDatabase::addFolder(FolderInfo &folder)
        {
            assert(folder.folderId == -1 && "Folder ID must be -1 for addFolder");
            if (folder.path.empty())
            {
                spdlog::error("addFolder: Folder path cannot be empty.");
                return false;
            }
            if (folder.name.empty())
            {
                spdlog::error("addFolder: Folder name cannot be empty.");
                return false;
            }

            const char *sql = "INSERT INTO Folders (parent_id, name, root_path, actual_path) VALUES (?, ?, ?, ?);";
            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("addFolder: Failed to prepare statement. DB error: {}", m_db.getLastError());
                return false;
            }

            (folder.parentId > 0) ? stmt.addParam(folder.parentId) : stmt.addNullParam();
            stmt.addParam(folder.name);
            stmt.addParam(folder.path);
            stmt.addParam(folder.actualPath.empty() ? folder.path : folder.actualPath); // If no actual path, use normalized

            if (!stmt.execute())
            {
                spdlog::error("addFolder: Failed to execute INSERT. DB error: {}", m_db.getLastError());
                return false;
            }

            folder.folderId = m_db.getLastInsertRowId();

            std::lock_guard lock{m_cacheMutex};
            if (m_isCacheValid)
            {
                m_folderInfoFromId[folder.folderId] = folder;
                m_idFromFolderPath[folder.path] = folder.folderId;
                registerAsParent(folder.parentId, folder.folderId);

                spdlog::trace("Surgically added new folder {} to caches.", folder.folderId);
            }
            // No longer calling invalidateCache() here.
            return true;
        }

        void SqliteFolderDatabase::insertParentsRecursive(FolderId folderId, std::unordered_set<FolderId> &foldersInUse) const
        {
            // This function recursively adds all parent folders to the set
            if (folderId <= 0 || foldersInUse.contains(folderId))
                return;

            foldersInUse.insert(folderId);
            const auto info = m_folderInfoFromId.find(folderId);
            if (info != m_folderInfoFromId.end())
            {
                // If the parent is not already in the set, recurse
                insertParentsRecursive(info->second.parentId, foldersInUse);
            }
        }

        bool SqliteFolderDatabase::removeEmptyFolders() const
        {
            if (SqliteTransaction transaction{m_db})
            {
                // first, we check all folders that have files in them
                SqliteStatement selectStmt{m_db, "SELECT DISTINCT folder_id FROM Tracks"};
                std::unordered_set<FolderId> foldersInUse;
                while (selectStmt.getNextResult())
                {
                    const auto folderId{selectStmt.getInt64(0)};
                    if (folderId > 0)
                    {
                        // if this folder is new, also add all its parents
                        insertParentsRecursive(folderId, foldersInUse);
                    }
                }

                // Now, we need all folders that are not in use anywhere - that are not in the above list. We should be able to safely delete those.
                SqliteStatement deleteStmt{m_db, "DELETE FROM Folders WHERE folder_id=?"};
                for (const auto &item : m_folderInfoFromId)
                {
                    if (!foldersInUse.contains(item.first))
                    {
                        // This folder is not in use, we can delete it
                        deleteStmt.addParam(item.first);
                        if (!deleteStmt.execute())
                        {
                            spdlog::error("removeEmptyFolders: Failed to delete folder with ID: {}", item.first);
                            return transaction.rollback();
                        }
                        spdlog::debug("Removed empty folder with ID: {}", item.first);
                        deleteStmt.reset();
                    }
                    else
                    {
                        spdlog::trace("Folder {} is still in use, skipping deletion.", item.first);
                    }
                }

                std::ignore = SqliteStatement{m_db, "PRAGMA optimize;"}.execute();
                std::ignore = SqliteStatement{m_db, "VACUUM;"}.execute();
                if (transaction.commit())
                {
                    invalidateCache();
                    buildCacheIfNeeded();
                    return true;
                }
            }
            return false;
        }

        bool SqliteFolderDatabase::updateFolder(const FolderInfo &folder)
        {
            assert(folder.isValid() && "FolderInfo must be valid for updateFolder");

            const char *sql = "UPDATE Folders SET parent_id = ?, name = ?, root_path = ?, actual_path = ? WHERE folder_id = ?;";
            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("updateFolder: Failed to prepare statement. DB error: {}", m_db.getLastError());
                return false;
            }

            (folder.parentId != -1) ? stmt.addParam(folder.parentId) : stmt.addNullParam();
            stmt.addParam(folder.name);
            stmt.addParam(folder.path);
            stmt.addParam(folder.actualPath.empty() ? folder.path : folder.actualPath);
            stmt.addParam(folder.folderId);

            if (!stmt.execute())
            {
                spdlog::error("updateFolder: Failed to execute UPDATE. DB error: {}", m_db.getLastError());
                return false;
            }

            invalidateCache();
            return true;
        }

        FolderId SqliteFolderDatabase::findOrCreateFolderByPath(const std::filesystem::path &path)
        {
            std::lock_guard dbLock{m_db.getMutex()};
            buildCacheIfNeeded();

            const auto key{normalizeForCache(pathToString(path))};
            if (key.empty())
            {
                return -1; // Cannot process an empty path
            }

            const auto item{m_idFromFolderPath.find(key)};
            if (item != m_idFromFolderPath.end())
            {
                // Found existing folder - check if we need to update actual_path
                FolderId existingId = item->second;
                auto folderIt = m_folderInfoFromId.find(existingId);
                if (folderIt != m_folderInfoFromId.end() && folderIt->second.actualPath.empty())
                {
                    // Update the actual_path for this folder
                    std::string actualPath = pathToString(path);
                    spdlog::debug("Updating actual_path for folder {} to '{}'", existingId, actualPath);
                    
                    SqliteStatement stmt{m_db, "UPDATE Folders SET actual_path = ? WHERE folder_id = ?;"};
                    if (stmt.isValid())
                    {
                        stmt.addParam(actualPath);
                        stmt.addParam(existingId);
                        if (stmt.execute())
                        {
                            // Update the cache
                            folderIt->second.actualPath = actualPath;
                        }
                    }
                }
                return item->second;
            }

            // --- RECURSION BASE CASE ---
            // If the path has no parent (e.g., "C:\") or its parent is itself,
            // it's a root. We create it with no parent.
            FolderId parentId = -1;
            if (path.has_parent_path())
            {
                const auto parentDirectory = path.parent_path();
                if (parentDirectory != path) // Stop recursion if parent is same as current
                {
                    parentId = findOrCreateFolderByPath(parentDirectory);
                    if (parentId <= 0)
                    {
                        spdlog::error("findOrCreateFolderByPath: Failed to find or create parent folder for path '{}'", pathToString(parentDirectory));
                        return -1; // Indicate failure
                    }
                }
            }

            FolderInfo newFolder{};
            newFolder.parentId = parentId;
            newFolder.name = pathToString(path.filename());
            // For root paths like "C:\", filename() might be empty. Use the whole path.
            if (newFolder.name.empty())
            {
                newFolder.name = key;
            }
            newFolder.path = key;
            newFolder.actualPath = pathToString(path); // Store the actual case-preserving path
            assert(newFolder.trackCount == -1); // not yet known - will be calculated later
            if (addFolder(newFolder))
            {
                return newFolder.folderId;
            }
            return -1; // Indicate failure to create folder
        }

        void SqliteFolderDatabase::rebuildOfflineFoldersTable(ILibraryRootManager& rootManager)
        {
            spdlog::info("Rebuilding offline folders table...");
            
            // Create the temp table (don't drop it first to avoid race conditions)
            SqliteStatement createStmt{m_db};
            createStmt.bindStatement("CREATE TEMP TABLE IF NOT EXISTS OfflineFolders (folder_id INTEGER PRIMARY KEY);");
            if (!createStmt.execute())
            {
                spdlog::error("Failed to create OfflineFolders temp table");
                return;
            }
            
            // Clear existing contents
            SqliteStatement clearStmt{m_db};
            clearStmt.bindStatement("DELETE FROM temp.OfflineFolders;");
            std::ignore = clearStmt.execute();
            
            // Get all library roots and their status
            const auto roots = rootManager.getAllRoots();
            
            // Collect all offline folder IDs
            std::unordered_set<FolderId> offlineFolderIds;
            
            for (const auto &root : roots)
            {
                if (!root.isOnline)
                {
                    // This root is offline - find its folder ID and all children
                    const auto rootFolderId = findOrCreateFolderByPath(root.path);
                    if (rootFolderId > 0)
                    {
                        // Add the root folder itself
                        offlineFolderIds.insert(rootFolderId);
                        
                        // Add all child folders recursively
                        getChildFoldersRecursive(const_cast<std::unordered_set<FolderId>&>(offlineFolderIds), rootFolderId);
                    }
                }
            }
            
            // Insert all offline folder IDs into the temp table
            if (!offlineFolderIds.empty())
            {
                SqliteTransaction transaction{m_db};
                SqliteStatement insertStmt{m_db};
                
                for (const auto folderId : offlineFolderIds)
                {
                    insertStmt.reset();
                    insertStmt.bindStatement("INSERT INTO temp.OfflineFolders (folder_id) VALUES (?);");
                    insertStmt.addParam(folderId);
                    std::ignore = insertStmt.execute();
                }
                
                transaction.commit();
                spdlog::info("Added {} offline folders to temp table", offlineFolderIds.size());
                
                // Now create and populate OfflineWorkingSets table
                // This contains working sets that have at least one track from an offline folder
                spdlog::info("Building offline working sets table...");
                
                SqliteStatement createWsStmt{m_db};
                createWsStmt.bindStatement("CREATE TEMP TABLE IF NOT EXISTS OfflineWorkingSets (ws_id INTEGER PRIMARY KEY);");
                if (!createWsStmt.execute())
                {
                    spdlog::error("Failed to create OfflineWorkingSets temp table");
                }
                else
                {
                    // Clear and populate
                    SqliteStatement clearWsStmt{m_db};
                    clearWsStmt.bindStatement("DELETE FROM temp.OfflineWorkingSets;");
                    std::ignore = clearWsStmt.execute();
                    
                    // Find working sets with offline tracks
                    SqliteStatement findOfflineWs{m_db};
                    findOfflineWs.bindStatement(R"SQL(
                        INSERT INTO temp.OfflineWorkingSets (ws_id)
                        SELECT DISTINCT wst.ws_id 
                        FROM WorkingSetTracks wst
                        JOIN Tracks t ON wst.track_id = t.track_id
                        WHERE t.folder_id IN (SELECT folder_id FROM temp.OfflineFolders);
                    )SQL");
                    
                    if (findOfflineWs.execute())
                    {
                        const auto affectedRows = m_db.getChangesCount();
                        spdlog::info("Marked {} working sets as offline", affectedRows);
                    }
                }
                
                // Now create and populate OfflineMixes table
                // This contains mixes that have at least one track from an offline folder
                spdlog::info("Building offline mixes table...");
                
                SqliteStatement createMixStmt{m_db};
                createMixStmt.bindStatement("CREATE TEMP TABLE IF NOT EXISTS OfflineMixes (mix_id INTEGER PRIMARY KEY);");
                if (!createMixStmt.execute())
                {
                    spdlog::error("Failed to create OfflineMixes temp table");
                }
                else
                {
                    // Clear and populate
                    SqliteStatement clearMixStmt{m_db};
                    clearMixStmt.bindStatement("DELETE FROM temp.OfflineMixes;");
                    std::ignore = clearMixStmt.execute();
                    
                    // Find mixes with offline tracks
                    SqliteStatement findOfflineMix{m_db};
                    findOfflineMix.bindStatement(R"SQL(
                        INSERT INTO temp.OfflineMixes (mix_id)
                        SELECT DISTINCT mt.mix_id 
                        FROM MixTracks mt
                        JOIN Tracks t ON mt.track_id = t.track_id
                        WHERE t.folder_id IN (SELECT folder_id FROM temp.OfflineFolders);
                    )SQL");
                    
                    if (findOfflineMix.execute())
                    {
                        const auto affectedRows = m_db.getChangesCount();
                        spdlog::info("Marked {} mixes as offline", affectedRows);
                    }
                }
            }
            else
            {
                spdlog::info("No offline folders found");
                
                // Drop the temp tables if they exist (all roots are online)
                SqliteStatement dropWsTable{m_db};
                dropWsTable.bindStatement("DROP TABLE IF EXISTS temp.OfflineWorkingSets;");
                std::ignore = dropWsTable.execute();
                
                SqliteStatement dropMixTable{m_db};
                dropMixTable.bindStatement("DROP TABLE IF EXISTS temp.OfflineMixes;");
                std::ignore = dropMixTable.execute();
            }
        }

    } // namespace database
} // namespace jucyaudio
