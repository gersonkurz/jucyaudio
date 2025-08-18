#include <Database/Includes/AlbumInfo.h>
#include <Database/Includes/ILibraryRootManager.h>
#include <Database/Sqlite/SqliteFolderDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Utils/AssortedUtils.h> // For pathToString etc.
#include <Utils/StringWriter.h>
#include <cassert>
#include <chrono>
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

            SqliteStatement stmt{m_db, "SELECT folder_id, parent_id, name, root_path, actual_path FROM Folders;"};
            if (!stmt.isValid())
            {
                spdlog::error("buildCacheIfNeeded: Failed to prepare statement. DB error: {}", m_db.getLastError());
                return false;
            }

            //size_t rowCount = 0;
            //size_t lookupCacheInserts = 0;

            // lookup list for updates of the root_path value
            std::unordered_map<FolderId, std::string> pathUpdates;

            spdlog::info("buildCacheIfNeeded: Fetching all folders from the database...");

            FolderInfo info{};
            while (stmt.getNextResult())
            {
                info.folderId = stmt.getInt64(0);
                info.parentId = stmt.isNull(1) ? -1 : stmt.getInt64(1);
                info.name = stmt.getText(2);
                info.path = stmt.isNull(3) ? "" : stmt.getText(3);
                info.actualPath = stmt.isNull(4) ? "" : stmt.getText(4);
                info.trackCount = 0;

                m_folderInfoFromId[info.folderId] = info;
                if (info.parentId > 0)
                {
                    registerAsParent(info.parentId, info.folderId);

                    std::vector<FolderId> parentIds;
                    std::list<std::string> pathSegments;
                    for (auto currentId = info.folderId;;)
                    {
                        parentIds.push_back(currentId);
                        const auto pf{m_folderInfoFromId.find(currentId)};
                        if (pf == m_folderInfoFromId.end())
                        {
                            spdlog::error("buildCacheIfNeeded: unable to build up cache, missing parent folder for ID {}", currentId);
                            return false;
                        }
                        if (info.path.empty())
                        {
                            pathSegments.push_front(pf->second.name);
                        }
                        if (pf->second.parentId <= 0)
                        {
                            break; // Reached the root or no parent
                        }
                        currentId = pf->second.parentId;
                    }
                    if (!parentIds.empty())
                    {
                        m_parentsFromChildren[info.folderId] = std::move(parentIds);
                    }

                    // If the path is empty, we need to build it from segments
                    if (info.path.empty())
                    {
                        StringWriter pathWriter;
                        for (const auto &segment : pathSegments)
                        {
                            if (!pathWriter.empty() && !pathWriter.endsWith("\\"))
                            {
                                pathWriter.append("\\");
                            }
                            pathWriter.append(segment);
                        }

                        info.path = normalizeForCache(pathWriter.asString());
                        pathUpdates[info.folderId] = info.path; // Store the update for later
                    }

                    const auto pf{m_idFromFolderPath.find(info.path)};
                    if (pf != m_idFromFolderPath.end())
                    {
                        spdlog::error("Found duplicate: Folder {} already exists in m_folderInfoFromId", info.path);
                        spdlog::error("New ID is {}", info.folderId);
                        spdlog::error("Existing ID is {}", pf->second);
                        return false;
                    }
                }
                else
                {
                    info.path = info.name; // Root folder path is just its name
                }
                m_idFromFolderPath[info.path] = info.folderId;
            }
            spdlog::info("buildCacheIfNeeded: complete with {} folders loaded.", m_folderInfoFromId.size());

            if (!pathUpdates.empty())
            {
                updateRootPathValuesInDatabase(pathUpdates);
            }
            spdlog::info("BEGIN recursive track count calculation");

            struct ExistingAlbumInfo
            {
                AlbumId albumId;
                std::string albumArtist;
                std::string title;
            };

            struct NewAlbumInfo
            {
                std::string albumArtist;
                std::string title;
                FolderId folderId;
            };

            // build lookup map of existing albums by folder ID
            std::unordered_map<FolderId, ExistingAlbumInfo> albumsByFolder;
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

                // Other fields are not used in this context, so we can skip them
                albumsByFolder[folderId] = std::move(albumInfo);
            }

            FolderId lastKnownFolderId = -1;
            std::string lastKnownArtistName;
            std::string lastKnownAlbumName;
            bool useThisFolder = true;
            bool folderAlreadyHasAlbum = false;

            // TODO: read in existing albums first. Right now, we assume there are no albums in the database.
            std::vector<NewAlbumInfo> albums;
            std::unordered_set<FolderId> albumFolders;

            SqliteStatement countStmt{m_db, "SELECT track_id, folder_id, artist_name, album_title FROM Tracks ORDER BY folder_ID ASC"};
            while (countStmt.getNextResult())
            {
                //const TrackId trackId = countStmt.getInt64(0);
                const FolderId folderId = countStmt.getInt64(1);
                const std::string artistName = countStmt.getText(2);
                const std::string albumName = countStmt.getText(3);

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
                            else if (!lastKnownArtistName.empty() && !lastKnownArtistName.empty())
                            {
                                // Create a new album info entry
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
                        // spdlog::warn("Track {} has empty artist name, skipping for folder ID {}", trackId, folderId);
                        useThisFolder = false; // Skip this folder for album creation
                    }
                    else if (albumName.empty())
                    {
                        // spdlog::warn("Track {} has empty album name, skipping for folder ID {}", trackId, folderId);
                        useThisFolder = false; // Skip this folder for album creation
                    }
                    else if (lastKnownArtistName.empty())
                    {
                        // check if this folder already has an album
                        auto item = albumsByFolder.find(folderId);
                        if (item != albumsByFolder.end())
                        {
                            if (item->second.albumArtist != artistName || item->second.title != albumName)
                            {
                                // spdlog::warn("Folder {} already has an album with different artist/album, skipping for folder ID {}", folderId, folderId);
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
                        // spdlog::warn("Folder has two or more artists/albums, skipping for folder ID {}", folderId);
                        lastKnownArtistName.clear();
                        lastKnownAlbumName.clear();
                        useThisFolder = false; // Skip this folder for album creation
                    }
                }

                auto item = m_parentsFromChildren.find(folderId);
                if (item != m_parentsFromChildren.end())
                {
                    for (const auto folderId : item->second)
                    {
                        auto it = m_folderInfoFromId.find(folderId);
                        if (it != m_folderInfoFromId.end())
                        {
                            ++(it->second.trackCount);
                        }
                    }
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
            return m_childrenFromParents.find(parentId) != m_childrenFromParents.end();
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
                        children.push_back(folderIt->second);
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

                SqliteStatement{m_db, "PRAGMA optimize;"}.execute();
                SqliteStatement{m_db, "VACUUM;"}.execute();
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
            newFolder.trackCount = -1; // not yet known - will be calculated later
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
            clearStmt.execute();
            
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
                    insertStmt.execute();
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
                    clearWsStmt.execute();
                    
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
                    clearMixStmt.execute();
                    
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
                dropWsTable.execute();
                
                SqliteStatement dropMixTable{m_db};
                dropMixTable.bindStatement("DROP TABLE IF EXISTS temp.OfflineMixes;");
                dropMixTable.execute();
            }
        }

    } // namespace database
} // namespace jucyaudio