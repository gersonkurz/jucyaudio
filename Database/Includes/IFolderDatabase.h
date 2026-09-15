#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/FolderInfo.h>
#include <optional>
#include <unordered_set>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        /**
         * @brief Defines the interface for managing the hierarchical folder structure.
         */
        class IFolderDatabase
        {
        public:
            virtual ~IFolderDatabase() = default;

            /**
             * @brief Retrieves a single folder's information by its unique ID.
             * @param folderId The ID of the folder to retrieve.
             * @return A FolderInfo struct if found, otherwise std::nullopt.
             */
            virtual std::optional<FolderInfo> getFolderById(FolderId folderId) const = 0;

            /**
             * @brief Retrieves the immediate children of a given parent folder.
             * @param parentId The ID of the parent folder. Use -1 to get root-level folders.
             * @return A vector of FolderInfo structs for all direct children.
             */
            virtual std::vector<FolderInfo> getChildFolders(FolderId parentId) const = 0;

            virtual bool hasChildren(FolderId parentId) const = 0;

            /**
             * @brief Builds whatever the implementation caches, and says whether it worked.
             *
             * Returned rather than discarded because a cache that cannot be built is not a library
             * with no folders in it: the reads that fill it can fail, and every accessor afterwards
             * answers out of whatever was read before the failure. A caller that ignores this is
             * choosing to serve that, which is a decision worth making on purpose.
             */
            virtual DbResult initialize()
            {
                return DbResult::success();
            }

            /**
             * @brief Adds a new folder to the database.
             * @param folder A FolderInfo struct to add. The folderId should be -1.
             *               On success, the struct's folderId will be updated with the new ID.
             * @return True on success, false on failure.
             */
            virtual bool addFolder(FolderInfo &folder) = 0;

            virtual std::unordered_set<FolderId> getAllChildFolders(const std::vector<FolderId> &folderIdsToScan) const = 0;

            /**
             * @brief The same walk, but it says whether the cache underneath it was whole.
             *
             * The form above cannot: an incomplete set and a complete one are the same type, and a
             * folder cache built from reads that failed partway holds a prefix of the tree. Callers
             * that only display the answer can live with that. Callers that decide what to scan, what
             * is in scope, or what is missing cannot - a short set there means folders nobody looked
             * at, and a track under one of them is indistinguishable from a deleted file.
             *
             * @param results Cleared first, then filled with whatever the cache could answer -
             *        including a partial tree when the build failed, so a caller that wants to show
             *        what it got still can.
             * @return Ok when the cache was built whole, otherwise the failure, with @p results
             *         holding what was readable.
             */
            virtual DbResult getAllChildFolders(const std::vector<FolderId> &folderIdsToScan, std::unordered_set<FolderId> &results) const = 0;

            virtual bool removeEmptyFolders() const = 0;

            /**
             * @brief Updates the data for an existing folder.
             * @param folder The FolderInfo struct with updated data. The folderId must be valid.
             * @return True on success, false on failure.
             */
            virtual bool updateFolder(const FolderInfo &folder) = 0;

            /**
             * @brief Invalidates the internal cache, forcing a reload from the database on next access.
             */
            virtual void invalidateCache() const = 0;

            
             /**
             * @brief Finds a folder by its full path, creating it and its parents if they don't exist.
             * This is the primary method for mapping a filesystem path to a folder ID during scans.
             * @param path The full, absolute path to the folder.
             * @return The ID of the folder, or -1 on failure.
             */
            virtual FolderId findOrCreateFolderByPath(const std::filesystem::path &path) = 0;


            virtual std::unordered_set<FolderId> getParentSet(FolderId folderId) const = 0;
        };
    } // namespace database
} // namespace jucyaudio