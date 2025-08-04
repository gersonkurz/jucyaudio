// Tentative content for Engine/TrackQueryArgs.h
#pragma once
#include <Database/Includes/Constants.h>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        constexpr size_t QUERY_PAGE_SIZE = 1024;

        struct TrackQueryArgs
        {
            std::vector<std::string> columns;
            std::vector<std::string> searchTerms;
            std::vector<SortOrderInfo> sortBy;
            RowIndex_t offset{0};
            
            WorkingSetId workingSetId{0};
            MixId mixId{0};
            bool usePaging{true};
            std::vector<FolderId> folderIds;        ///< Filter tracks by a list of parent folder IDs.
            bool recursive{false};                   ///< If true, also include tracks in all subfolders of folderIds.
        };

    } // namespace database
} // namespace jucyaudio