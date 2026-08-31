#pragma once

#include <Database/Includes/IFolderDatabase.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace jucyaudio
{
    namespace database
    {
        // Forward declaration
        class ILibraryRootManager;

        class SqliteFolderDatabase : public IFolderDatabase
        {
        public:
            explicit SqliteFolderDatabase(database::SqliteDatabase &db);
            ~SqliteFolderDatabase() override = default;

            /**
             * @brief Retrieves a single folder's information by its unique ID.
             * @param folderId The ID of the folder to retrieve.
             * @return A FolderInfo struct if found, otherwise std::nullopt.
             */
            std::optional<FolderInfo> getFolderById(FolderId folderId) const override;

            /**
             * @brief Retrieves the immediate children of a given parent folder.
             * @param parentId The ID of the parent folder. Use -1 to get root-level folders.
             * @return A vector of FolderInfo structs for all direct children.
             */
            std::vector<FolderInfo> getChildFolders(FolderId parentId) const override;
            std::unordered_set<FolderId> getParentSet(FolderId folderId) const override;
            bool hasChildren(FolderId parentId) const override;
            bool removeEmptyFolders() const override;
            bool updateFolder(const FolderInfo &folder) override;
            void invalidateCache() const override;
            FolderId findOrCreateFolderByPath(const std::filesystem::path &path) override;
            std::unordered_set<FolderId> getAllChildFolders(const std::vector<FolderId> &folderIdsToScan) const override;

            void initialize() override
            {
                buildCacheIfNeeded();
            }

            /**
             * @brief Rebuilds the temp table of offline folders based on current library root status.
             * This creates/updates a temp table that can be used in SQL queries to filter offline tracks.
             * @param rootManager The library root manager to use for getting root statuses
             */
            void rebuildOfflineFoldersTable(ILibraryRootManager& rootManager);

        private:
            bool addFolder(FolderInfo &folder) override;

            /// @brief Adds a folder and every parent of it to @p foldersInUse.
            ///        Call with m_cacheMutex held, for the same reason as getChildFoldersRecursive.
            void insertParentsRecursive(FolderId folderId, std::unordered_set<FolderId> &foldersInUse) const;

            void registerAsParent(FolderId parentId, FolderId folderId) const
            {
                // This function is used to register a folder as a child of its parent
                const auto it = m_childrenFromParents.find(parentId);
                if (it == m_childrenFromParents.end())
                {
                    m_childrenFromParents[parentId] = std::vector<FolderId>{folderId};
                }
                else
                {
                    it->second.push_back(folderId);
                }
            }
            /// @brief Loads all folders from the database into the cache if it's empty.
            bool buildCacheIfNeeded() const;

            /// @brief Helper function that updates root_path values in the database, once.
            bool updateRootPathValuesInDatabase(const std::unordered_map<FolderId, std::string> &pathUpdates) const;

            /// @brief Walks the child index. Call with m_cacheMutex held: it reads the cache and
            ///        recurses, so taking the mutex inside would need it to be recursive for no reason.
            void getChildFoldersRecursive(std::unordered_set<FolderId> &allChildIds, FolderId folderId) const;

            database::SqliteDatabase &m_db;

            // lookup all data for a folder 
            mutable std::unordered_map<FolderId, FolderInfo> m_folderInfoFromId;
            mutable std::unordered_map<std::string, FolderId> m_idFromFolderPath;
            mutable std::unordered_map<FolderId, std::vector<FolderId>> m_childrenFromParents; 
            mutable std::unordered_map<FolderId, std::vector<FolderId>> m_parentsFromChildren; 

            mutable bool m_isCacheValid{false};

            /// @brief Guards the four maps above and m_isCacheValid.
            ///
            /// Lock order, where both are needed: the database mutex first, then this one. It used to
            /// be taken in both orders - buildCacheIfNeeded held this one and then reached for the
            /// database mutex through SqliteStatement, while findOrCreateFolderByPath held the database
            /// mutex and then wanted this one - so two threads could hold one each and wait for the
            /// other. That window was narrowest during a scan, which is when both paths run hardest.
            ///
            /// Corollary, and the easier rule to check when adding code here: do not run a statement
            /// while holding this mutex unless the database mutex is already held. Everything except
            /// buildCacheIfNeeded and the lookup in findOrCreateFolderByPath now reads the maps under
            /// this mutex alone, with no SQL inside.
            mutable std::mutex m_cacheMutex;
        };
    } // namespace database
} // namespace jucyaudio
