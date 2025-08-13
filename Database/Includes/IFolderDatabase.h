#pragma once

#include <Database/Includes/FolderInfo.h>
#include <optional>
#include <vector>
#include <unordered_set>

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
             * @brief Adds a new folder to the database.
             * @param folder A FolderInfo struct to add. The folderId should be -1.
             *               On success, the struct's folderId will be updated with the new ID.
             * @return True on success, false on failure.
             */
            virtual bool addFolder(FolderInfo &folder) = 0;

            virtual std::unordered_set<FolderId> getAllChildFolders(const std::vector<FolderId> &folderIdsToScan) const = 0;

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