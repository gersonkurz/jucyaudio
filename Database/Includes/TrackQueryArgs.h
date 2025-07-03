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
            std::vector<std::string> searchTerms;
            std::vector<SortOrderInfo> sortBy;
            RowIndex_t offset{0};
            std::optional<std::filesystem::path> pathFilter;
            WorkingSetId workingSetId{0};
            MixId mixId{0};
            bool usePaging{true};
        };

    } // namespace database
} // namespace jucyaudio