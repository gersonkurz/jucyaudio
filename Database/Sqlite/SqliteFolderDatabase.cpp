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
        SqliteFolderDatabase::SqliteFolderDatabase(database::SqliteDatabase &db)
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

            SqliteStatement stmt{m_db, "SELECT folder_id, parent_id, name, root_path FROM Folders;"};
            if (!stmt.isValid())
            {
                spdlog::error("buildCacheIfNeeded: Failed to prepare statement. DB error: {}", m_db.getLastError());
                return false;
            }

            size_t rowCount = 0;
            size_t lookupCacheInserts = 0;

            // lookup list for updates of the root_path value
            std::unordered_map<FolderId, std::string> pathUpdates;

            // we should probably allocate all these in one huge cache
            std::unordered_map<FolderId, FolderInfo> flatFolderLookup;

            spdlog::info("buildCacheIfNeeded: Fetching all folders from the database...");

            FolderInfo info{};
            while (stmt.getNextResult())
            {
                info.folderId = stmt.getInt64(0);
                info.parentId = stmt.isNull(1) ? -1 : stmt.getInt64(1);
                info.name = stmt.getText(2);
                info.path = stmt.isNull(3) ? "" : stmt.getText(3);

                flatFolderLookup[info.folderId] = info;
                if (info.parentId > 0)
                {
                    registerAsParent(info.parentId, info.folderId);

                    std::list<std::string> pathSegments;
                    for (auto currentId = info.folderId;;)
                    {
                        const auto pf{flatFolderLookup.find(currentId)};
                        if (pf == flatFolderLookup.end())
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
                        spdlog::error("Found duplicate: Folder {} already exists in flatFolderLookup", info.path);
                        spdlog::error("New ID is {}", info.folderId);
                        spdlog::error("Existing ID is {}", pf->second);
                        return false;
                    }
                }
                else
                {
                    info.path = info.name; // Root folder path is just its name
                }
                m_folderInfoFromId[info.folderId] = info;
                m_idFromFolderPath[info.path] = info.folderId;
            }
            spdlog::info("buildCacheIfNeeded: complete with {} folders loaded.", m_folderInfoFromId.size());

            if (!pathUpdates.empty())
            {
                updateRootPathValuesInDatabase(pathUpdates);
            }

            m_isCacheValid = true;
            return true;
        }

        void SqliteFolderDatabase::getChildFoldersRecursive(std::unordered_set<FolderId>& allChildIds, FolderId folderId) const
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
            if(SqliteTransaction transaction{m_db})
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

            const char *sql = "INSERT INTO Folders (parent_id, name, root_path) VALUES (?, ?, ?);";
            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("addFolder: Failed to prepare statement. DB error: {}", m_db.getLastError());
                return false;
            }

            (folder.parentId > 0) ? stmt.addParam(folder.parentId) : stmt.addNullParam();
            stmt.addParam(folder.name);
            stmt.addParam(folder.path);

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

            const char *sql = "UPDATE Folders SET parent_id = ?, name = ?, root_path = ? WHERE folder_id = ?;";
            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("updateFolder: Failed to prepare statement. DB error: {}", m_db.getLastError());
                return false;
            }

            (folder.parentId != -1) ? stmt.addParam(folder.parentId) : stmt.addNullParam();
            stmt.addParam(folder.name);
            stmt.addParam(folder.path);
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
            const auto item{m_idFromFolderPath.find(key)};
            if (item != m_idFromFolderPath.end())
            {
                return item->second;
            }
            
            const std::filesystem::path parentDirectory = path.parent_path();
            const auto parentId{findOrCreateFolderByPath(parentDirectory)};
            if (parentId <= 0)
            {
                spdlog::error("findOrCreateFolderByPath: Failed to find or create parent folder for path '{}'", pathToString(parentDirectory));
                return -1; // Indicate failure to find or create parent folder
            }

            FolderInfo newFolder{};
            newFolder.parentId = parentId;
            newFolder.name = pathToString(path.filename());
            newFolder.path = key;
            if (addFolder(newFolder))
            {
                return newFolder.folderId;
            }
            return -1; // Indicate failure to create folder
        }

    } // namespace database
} // namespace jucyaudio