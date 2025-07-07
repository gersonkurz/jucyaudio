#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>


#define NGAUDIO_ENGINE_VERSION "0.1.0"

namespace jucyaudio
{
    using Duration_t = std::chrono::milliseconds;
    using Timestamp_t = std::chrono::system_clock::time_point;
    typedef uint64_t RowIndex_t;
    typedef uint32_t ColumnIndex_t;
    typedef int64_t TrackId; // For clarity, we can use a typedef for track IDs
    typedef int64_t TagId;   // For tag IDs
    typedef int64_t WorkingSetId;
    typedef int64_t MixId;
    typedef int64_t Volume_t;
    typedef int64_t BPM_t; // Beats per minute, stored as an integer*100 to avoid floating-point issues
    typedef int64_t FolderId;

    namespace database
    {
        constexpr Volume_t VOLUME_NORMALIZATION = 1000; // Normalization factor for volume, to prevent floating-point issues
        constexpr BPM_t BPM_NORMALIZATION = 1000;    // Normalization factor for BPM, to prevent floating-point issues

        struct TrackInfo; // Forward declaration

        typedef long long TrackId; // For clarity, we can use a typedef for track IDs
        typedef long long TagId;   // For tag IDs

        struct SortOrderInfo
        {
            std::string columnName; // From DataColumn::name
            bool descending;
        };

        // --- Concrete Struct for Column Definition ---
        enum class ColumnAlignment
        {
            Left,
            Center,
            Right
        }; // For UI formatting

        enum class ColumnDataTypeHint
        {
            String,
            Integer,
            Double,
            Date,
            Duration,
            Rating
        }; // For UI formatting

        /// @brief Represents the action to be performed on a data item in the UI.
        /// Currently, these are examples only: as we go along, we will modify these.
        enum class DataAction
        {
            None,          // No action
            Play,          // Play the item
            CreateWorkingSet, // create working set out of items currently shown
            ShowDetails,   // Show details in a separate view
            EditMetadata,  // Edit metadata of the item
            Delete,         // Delete the item
            CreateMix,     // Create a mix from the item(s) selected
            RemoveMix,
            ExportMix, // Export mix to file
        };

        using DataActions = std::vector<DataAction>;

        struct TagInfo
        { // Simple struct to return both ID and Name
            TagId id;
            std::string name;
        };

        struct WorkingSetInfo
        {
            WorkingSetId id;
            std::string name;
            Timestamp_t timestamp; // When the working set was created or last modified
            int64_t track_count;
            Duration_t total_duration; // Total duration of all tracks in the working set
        };


    }
}
