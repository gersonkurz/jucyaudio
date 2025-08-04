#include <Database/Sqlite/SqliteFolderDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Utils/AssortedUtils.h> // For pathToString etc.
#include <cassert>
#include <spdlog/spdlog.h>

struct FolderCacheKey
{
    jucyaudio::FolderId parentId;
    std::string normalizedName;

    bool operator==(const FolderCacheKey &other) const
    {
        return parentId == other.parentId && normalizedName == other.normalizedName;
    }
};

namespace std
{
    template <> struct hash<FolderCacheKey>
    {
        size_t operator()(const FolderCacheKey &k) const
        {
            return hash<jucyaudio::FolderId>()(k.parentId) ^ (hash<string>()(k.normalizedName) << 1);
        }
    };
} // namespace std

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
            // This operation is complex and involves multiple reads and potential writes.
            // It must be thread-safe. The lock on the DB's mutex ensures this.
            std::lock_guard dbLock(m_db.getMutex());

            // Ensure the in-memory cache of the folder tree is built and ready.
            buildCacheIfNeeded();

            // We need a secondary cache for this specific operation to map normalized path parts to IDs.
            // This is separate from m_folderCache and is only used for the duration of this call.
            std::unordered_map<FolderCacheKey, FolderId> lookupCache;
            for (const auto &[id, folder] : m_folderCache)
            {
                if (auto normalized = normalizeForCache(folder.name))
                {
                    lookupCache[{folder.parentId, *normalized}] = folder.folderId;
                }
            }

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
                    return -1; // Return -1 to indicate failure
                }

                FolderCacheKey key = {currentParentId, *normalizedResult};

                auto it = lookupCache.find(key);
                if (it != lookupCache.end())
                {
                    currentParentId = it->second;
                }
                else
                {
                    // Not found in the cache, so it doesn't exist in the DB. We must create it.
                    FolderInfo newFolder;
                    newFolder.parentId = currentParentId;
                    newFolder.name = part;
                    if (currentParentId == -1)
                    {
                        newFolder.rootPath = pathToString(path.root_path());
                    }

                    // The addFolder method will handle the INSERT and update the folderId.
                    if (addFolder(newFolder))
                    {
                        currentParentId = newFolder.folderId;
                        // Add the newly created folder to our temporary lookup cache to find its children.
                        lookupCache[key] = currentParentId;
                    }
                    else
                    {
                        spdlog::error("findOrCreateFolderByPath: Failed to add new folder '{}' to the database.", part);
                        return -1; // Return -1 on failure.
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
            spdlog::debug("Folder cache invalidated.");
        }

        void SqliteFolderDatabase::buildCacheIfNeeded() const
        {
            std::lock_guard lock(m_cacheMutex);
            if (m_isCacheValid)
            {
                return;
            }

            spdlog::debug("Building folder cache...");
            m_folderCache.clear();

            SqliteStatement stmt{m_db, "SELECT folder_id, parent_id, name, root_path FROM Folders;"};
            if (!stmt.isValid())
            {
                spdlog::error("buildCacheIfNeeded: Failed to prepare statement. DB error: {}", m_db.getLastError());
                return;
            }

            while (stmt.getNextResult())
            {
                FolderInfo info = getFolderInfoFromStatement(stmt);
                m_folderCache[info.folderId] = info;
            }
            m_isCacheValid = true;
            spdlog::debug("Folder cache built with {} entries.", m_folderCache.size());
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

        std::vector<FolderInfo> SqliteFolderDatabase::getChildFolders(FolderId parentId) const
        {
            buildCacheIfNeeded();
            std::lock_guard lock(m_cacheMutex);

            std::vector<FolderInfo> children;
            for (const auto &[id, folder] : m_folderCache)
            {
                if (folder.parentId == parentId)
                {
                    children.push_back(folder);
                }
            }

            // Sort children by name for consistent UI presentation
            std::sort(children.begin(),
                children.end(),
                [](const FolderInfo &a, const FolderInfo &b)
                {
                    return a.name < b.name; // This should ideally be a case-insensitive, natural sort
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
            invalidateCache(); // The cache is now stale.
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