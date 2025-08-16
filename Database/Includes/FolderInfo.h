#pragma once

#include <Database/Includes/Constants.h>
#include <filesystem>
#include <string>

namespace jucyaudio
{
    namespace database
    {
        /**
         * @brief Represents a single folder (directory) in the hierarchical library structure.
         * This struct directly maps to a row in the new `Folders` table.
         */
        struct FolderInfo final
        {
            /// @brief The unique identifier for this folder in the database. Primary Key.
            FolderId folderId{-1};

            /// @brief The folder_id of the parent folder. A value of -1 indicates this is a root-level folder.
            FolderId parentId{-1};

            /// @brief The name of this specific folder (e.g., "2025" or "Vaporwave").
            std::string name;

            /// @brief The full path of this folder on the filesystem (normalized for cache lookups).
            std::string path;

            /// @brief The actual case-preserving path of this folder (for file operations).
            /// If empty, falls back to using the normalized path.
            std::string actualPath;

            /// @brief The total number of tracks in this folder and all its subfolders.
            int64_t trackCount{-1}; 

            /// @brief A utility function to check if the struct contains valid data from the database.
            bool isValid() const
            {
                return folderId >= 0 && !name.empty();
            }
        };

    } // namespace database
} // namespace jucyaudio