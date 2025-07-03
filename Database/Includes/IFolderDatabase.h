#pragma once

#include <Database/Includes/FolderInfo.h>

namespace jucyaudio
{
    namespace database
    {

        // Data structure to hold info about a folder for the TableListBox
        // ...existing code...

        struct IFolderDatabase
        {
            virtual ~IFolderDatabase() = default;

            /**
             * Retrieves all folders from the database.
             * @param folders Output vector to be filled with FolderInfo entries.
             * @return true if the operation was successful, false otherwise.
             */
            virtual bool getFolders(std::vector<FolderInfo> &folders) const = 0;

            /**
             * Adds a new folder to the database.
             * @param folder The FolderInfo describing the folder to add. This is not a const reference, because it will get the new folderId
             * @return true if the folder was added successfully, false otherwise.
             */
            virtual bool addFolder(FolderInfo &folder) = 0;

            /**
             * Removes a folder from the database by its ID.
             * @param folderIdToRemove The ID of the folder to remove.
             * @return true if the folder was removed successfully, false otherwise.
             */
            virtual bool removeFolder(FolderId folderIdToRemove) = 0;

            /**
             * Removes all folders from the database.
             * @return true if all folders were removed successfully, false otherwise.
             */
            virtual bool removeAllFolders() = 0;

            /**
             * Updates the information of an existing folder in the database.
             * @param folder The updated FolderInfo.
             * @return true if the folder was updated successfully, false otherwise.
             */
            virtual bool updateFolder(const FolderInfo &folder) = 0;
        };

    } // namespace database
} // namespace jucyaudio