#include <Database/Sqlite/SqliteFolderDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Utils/AssortedUtils.h> // For pathToString etc.
#include <cassert>
#include <spdlog/spdlog.h>
#include <chrono>



namespace jucyaudio
{
    namespace database
    {
        SqliteFolderDatabase::SqliteFolderDatabase(database::SqliteDatabase &db)
            : m_db(db)
        {
        }

        FolderId SqliteFolderDatabase::findOrCreateFolderByPath(const std::filesystem::path &path)
        {
            std::lock_guard dbLock(m_db.getMutex());
            buildCacheIfNeeded();

            FolderId currentParentId = -1;
            std::vector<std::string> parts;
            std::filesystem::path current = path;
            while (current.has_relative_path())
            {
                parts.insert(parts.begin(), pathToString(current.filename()));
                current = current.parent_path();
            }
            parts.insert(parts.begin(), pathToString(current.root_path()));

            for (const auto &part : parts)
            {
                if (part.empty() || part == "\\" || part == "/")
                    continue;

                auto normalizedResult = normalizeForCache(part);
                if (!normalizedResult)
                {
                    spdlog::error("findOrCreateFolderByPath: Could not normalize path component '{}'", part);
                    return -1;
                }

                FolderCacheKey key = {currentParentId, *normalizedResult};

                // FIXED: Direct, fast lookup on the member cache. No local cache is built.
                auto it = m_lookupCache.find(key);
                if (it != m_lookupCache.end())
                {
                    currentParentId = it->second;
                }
                else
                {
                    FolderInfo newFolder;
                    newFolder.parentId = currentParentId;
                    newFolder.name = part;
                    if (currentParentId == -1)
                    {
                        newFolder.rootPath = pathToString(path.root_path());
                    }

                    if (addFolder(newFolder))
                    {
                        currentParentId = newFolder.folderId;
                    }
                    else
                    {
                        spdlog::error("findOrCreateFolderByPath: Failed to add new folder '{}' to the database.", part);
                        return -1;
                    }
                }
            }
            return currentParentId;
        }

        void SqliteFolderDatabase::invalidateCache()
        {
            std::lock_guard lock(m_cacheMutex);
            m_isCacheValid = false;
            m_folderCache.clear();
            m_childIndexCache.clear();
            m_lookupCache.clear(); // FIXED: Clear the lookup cache as well.
            spdlog::debug("Folder cache invalidated.");
        }

        void SqliteFolderDatabase::buildCacheIfNeeded() const
        {
            std::lock_guard lock(m_cacheMutex);
            if (m_isCacheValid)
            {
                return;
            }

            auto startTime = std::chrono::high_resolution_clock::now();
            spdlog::debug("Building all folder caches...");
            
            m_folderCache.clear();
            m_childIndexCache.clear();
            m_lookupCache.clear(); // FIXED: Clear the lookup cache.

            auto queryStartTime = std::chrono::high_resolution_clock::now();
            SqliteStatement stmt{m_db, "SELECT folder_id, parent_id, name, root_path FROM Folders;"};
            if (!stmt.isValid())
            {
                spdlog::error("buildCacheIfNeeded: Failed to prepare statement. DB error: {}", m_db.getLastError());
                return;
            }

            size_t rowCount = 0;
            size_t lookupCacheInserts = 0;
            auto processingStartTime = std::chrono::high_resolution_clock::now();
            
            // Pre-reserve space to avoid rehashing
            m_folderCache.reserve(350000);  // Slightly more than 338K
            m_lookupCache.reserve(350000);
            
            // Pre-size the child cache for common cases
            // Most folders won't have children, but those that do might have many
            m_childIndexCache.reserve(120000);  // We saw 117K parent folders
            
            // Time tracking for each operation type
            std::chrono::nanoseconds sqliteTime{0};
            std::chrono::nanoseconds folderCacheTime{0};
            std::chrono::nanoseconds childCacheTime{0};
            std::chrono::nanoseconds lookupCacheTime{0};
            
            // OPTIMIZATION EXPERIMENT: Try to reduce allocations
            bool useOptimizedPath = true;
            
            if (useOptimizedPath)
            {
                // Pre-allocate a reusable FolderInfo to avoid 338K allocations
                FolderInfo tempInfo;
                
                while (stmt.getNextResult())
                {
                    auto loopStart = std::chrono::high_resolution_clock::now();
                    
                    // Get data directly without creating temporary
                    tempInfo.folderId = stmt.getInt64(0);
                    tempInfo.parentId = stmt.isNull(1) ? -1 : stmt.getInt64(1);
                    tempInfo.name = stmt.getText(2);
                    tempInfo.rootPath = stmt.isNull(3) ? "" : stmt.getText(3);
                    
                    auto afterSqlite = std::chrono::high_resolution_clock::now();
                    sqliteTime += (afterSqlite - loopStart);
                    
                    // Insert into folderCache (this will copy the strings)
                    m_folderCache.emplace(tempInfo.folderId, tempInfo);
                    auto afterFolderCache = std::chrono::high_resolution_clock::now();
                    folderCacheTime += (afterFolderCache - afterSqlite);
                    
                    // Reserve space in vector if needed to avoid reallocation
                    auto& childVec = m_childIndexCache[tempInfo.parentId];
                    if (childVec.capacity() == childVec.size())
                    {
                        childVec.reserve(childVec.size() * 2 + 1);
                    }
                    childVec.push_back(tempInfo.folderId);
                    auto afterChildCache = std::chrono::high_resolution_clock::now();
                    childCacheTime += (afterChildCache - afterFolderCache);

                    // Use emplace for lookup cache to avoid temporary
                    // TEMPORARILY RESTORED for release build test
                    if (auto normalized = normalizeForCache(tempInfo.name))
                    {
                        m_lookupCache.emplace(FolderCacheKey{tempInfo.parentId, *normalized}, tempInfo.folderId);
                        lookupCacheInserts++;
                    }
                    auto afterLookupCache = std::chrono::high_resolution_clock::now();
                    lookupCacheTime += (afterLookupCache - afterChildCache);
                    
                    rowCount++;
                    if (rowCount % 50000 == 0)
                    {
                        spdlog::debug("Processed {} folders so far...", rowCount);
                    }
                }
            }
            else
            {
                // Original code path for comparison
                while (stmt.getNextResult())
                {
                    auto loopStart = std::chrono::high_resolution_clock::now();
                    
                    FolderInfo info = getFolderInfoFromStatement(stmt);
                    auto afterSqlite = std::chrono::high_resolution_clock::now();
                    sqliteTime += (afterSqlite - loopStart);
                    
                    m_folderCache[info.folderId] = info;
                    auto afterFolderCache = std::chrono::high_resolution_clock::now();
                    folderCacheTime += (afterFolderCache - afterSqlite);
                    
                    m_childIndexCache[info.parentId].push_back(info.folderId);
                    auto afterChildCache = std::chrono::high_resolution_clock::now();
                    childCacheTime += (afterChildCache - afterFolderCache);

                    // For testing: use non-normalized name directly
                    m_lookupCache[{info.parentId, info.name}] = info.folderId;
                    lookupCacheInserts++;
                    auto afterLookupCache = std::chrono::high_resolution_clock::now();
                    lookupCacheTime += (afterLookupCache - afterChildCache);
                    
                    rowCount++;
                    if (rowCount % 50000 == 0)
                    {
                        spdlog::debug("Processed {} folders so far...", rowCount);
                    }
                }
            }
            
            m_isCacheValid = true;
            
            auto endTime = std::chrono::high_resolution_clock::now();
            auto queryTime = std::chrono::duration_cast<std::chrono::milliseconds>(processingStartTime - queryStartTime).count();
            auto processingTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - processingStartTime).count();
            auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
            
            spdlog::info("Folder cache built: {} folders, {} lookup entries", rowCount, lookupCacheInserts);
            spdlog::info("Cache timing: query prep {}ms, processing {}ms, total {}ms", 
                         queryTime, processingTime, totalTime);
            spdlog::info("Cache sizes: folderCache={}, childIndexCache={}, lookupCache={}", 
                         m_folderCache.size(), m_childIndexCache.size(), m_lookupCache.size());
            
            // Log operation breakdown
            auto sqliteMs = std::chrono::duration_cast<std::chrono::milliseconds>(sqliteTime).count();
            auto folderCacheMs = std::chrono::duration_cast<std::chrono::milliseconds>(folderCacheTime).count();
            auto childCacheMs = std::chrono::duration_cast<std::chrono::milliseconds>(childCacheTime).count();
            auto lookupCacheMs = std::chrono::duration_cast<std::chrono::milliseconds>(lookupCacheTime).count();
            
            spdlog::info("Operation breakdown: SQLite {}ms, folderCache {}ms, childCache {}ms, lookupCache {}ms",
                         sqliteMs, folderCacheMs, childCacheMs, lookupCacheMs);
            
            // Check if we're spending time elsewhere
            auto accountedTime = sqliteMs + folderCacheMs + childCacheMs + lookupCacheMs;
            spdlog::info("Total accounted: {}ms, unaccounted: {}ms", accountedTime, processingTime - accountedTime);
        }

        FolderInfo SqliteFolderDatabase::getFolderInfoFromStatement(SqliteStatement &stmt)
        {
            FolderInfo info;
            info.folderId = stmt.getInt64(0);
            info.parentId = stmt.isNull(1) ? -1 : stmt.getInt64(1);
            info.name = stmt.getText(2);
            info.rootPath = stmt.isNull(3) ? "" : stmt.getText(3);
            return info;
        }
        
        void SqliteFolderDatabase::getFolderInfoFromStatementInPlace(SqliteStatement &stmt, FolderInfo &info)
        {
            info.folderId = stmt.getInt64(0);
            info.parentId = stmt.isNull(1) ? -1 : stmt.getInt64(1);
            info.name = stmt.getText(2);
            info.rootPath = stmt.isNull(3) ? "" : stmt.getText(3);
        }

        std::optional<FolderInfo> SqliteFolderDatabase::getFolderById(FolderId folderId) const
        {
            buildCacheIfNeeded();
            std::lock_guard lock(m_cacheMutex);

            auto it = m_folderCache.find(folderId);
            if (it != m_folderCache.end())
            {
                return it->second;
            }
            return std::nullopt;
        }

        bool SqliteFolderDatabase::hasChildren(FolderId parentId) const
        {
            buildCacheIfNeeded();
            std::lock_guard lock(m_cacheMutex);

            // This is an O(1) average time lookup. Incredibly fast.
            auto it = m_childIndexCache.find(parentId);
            if (it != m_childIndexCache.end())
            {
                // If an entry exists for this parent, it must have children.
                // We also check that the vector is not empty for robustness.
                return !it->second.empty();
            }

            // No entry in the child index means no children.
            return false;
        }

        std::vector<FolderInfo> SqliteFolderDatabase::getChildFolders(FolderId parentId) const
        {
            buildCacheIfNeeded();
            std::lock_guard lock(m_cacheMutex);

            std::vector<FolderInfo> children;

            // 1. Perform a fast lookup in our child index cache.
            auto it = m_childIndexCache.find(parentId);
            if (it != m_childIndexCache.end())
            {
                const auto &childIds = it->second;
                children.reserve(childIds.size());

                // 2. For each child ID, get the full FolderInfo from the main cache.
                for (FolderId childId : childIds)
                {
                    auto folderIt = m_folderCache.find(childId);
                    if (folderIt != m_folderCache.end())
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
                    // A proper natural/locale-aware sort would be better here,
                    // but a simple case-insensitive one is a good start.
                    auto normA = normalizeForCache(a.name);
                    auto normB = normalizeForCache(b.name);
                    if (normA && normB)
                        return *normA < *normB;
                    return a.name < b.name;
                });
            return children;
        }

        bool SqliteFolderDatabase::addFolder(FolderInfo &folder)
        {
            assert(folder.folderId == -1 && "Folder ID must be -1 for addFolder");

            const char *sql = "INSERT INTO Folders (parent_id, name, root_path) VALUES (?, ?, ?);";
            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("addFolder: Failed to prepare statement. DB error: {}", m_db.getLastError());
                return false;
            }

            (folder.parentId != -1) ? stmt.addParam(folder.parentId) : stmt.addNullParam();
            stmt.addParam(folder.name);
            (!folder.rootPath.empty()) ? stmt.addParam(folder.rootPath) : stmt.addNullParam();

            if (!stmt.execute())
            {
                spdlog::error("addFolder: Failed to execute INSERT. DB error: {}", m_db.getLastError());
                return false;
            }

            folder.folderId = m_db.getLastInsertRowId();
            // --- FIXED: SURGICAL CACHE UPDATE INSTEAD OF INVALIDATION ---
            std::lock_guard lock(m_cacheMutex);
            if (m_isCacheValid)
            {
                // 1. Add to main folder cache
                m_folderCache[folder.folderId] = folder;
                // 2. Add to parent->child index
                m_childIndexCache[folder.parentId].push_back(folder.folderId);
                // 3. Add to lookup cache
                if (auto normalized = normalizeForCache(folder.name))
                {
                    m_lookupCache[{folder.parentId, *normalized}] = folder.folderId;
                }
                spdlog::trace("Surgically added new folder {} to caches.", folder.folderId);
            }
            // No longer calling invalidateCache() here.
            return true;
        }

        bool SqliteFolderDatabase::removeFolder(FolderId folderId)
        {
            // Note: ON DELETE CASCADE will handle removing child folders and tracks.
            SqliteStatement stmt{m_db, "DELETE FROM Folders WHERE folder_id = ?;"};
            if (!stmt.isValid() || !stmt.addParam(folderId) || !stmt.execute())
            {
                spdlog::error("removeFolder: Failed to execute DELETE. DB error: {}", m_db.getLastError());
                return false;
            }

            invalidateCache();
            return true;
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
            (!folder.rootPath.empty()) ? stmt.addParam(folder.rootPath) : stmt.addNullParam();
            stmt.addParam(folder.folderId);

            if (!stmt.execute())
            {
                spdlog::error("updateFolder: Failed to execute UPDATE. DB error: {}", m_db.getLastError());
                return false;
            }

            invalidateCache();
            return true;
        }

    } // namespace database
} // namespace jucyaudio