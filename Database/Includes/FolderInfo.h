#pragma once

#include <filesystem>
#include <Database/Includes/Constants.h>

namespace jucyaudio
{
    namespace database
    {
        typedef int64_t FolderId;

        // Data structure to hold info about a watched folder for the TableListBox
        struct FolderInfo final
        {
            FolderId folderId{-1}; // Unique ID for the watched folder, -1 means not set
            std::filesystem::path path;
            int numFiles{0};            // From last scan of this path, -1 if not scanned yet
            int64_t totalSizeBytes{0};  // From last scan
            Timestamp_t lastScannedTime; // From last scan

            bool isValid() const
            {
                return !path.empty() && (numFiles >= 0) && (folderId >= 0);
            }
        };

    } // namespace database
} // namespace jucyaudio