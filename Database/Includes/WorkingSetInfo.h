#pragma once

#include <Database/Includes/Constants.h>
#include <chrono>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        /**
         * @brief Complete information about a working set
         * @details Working sets are user-defined collections of tracks organized
         *          for specific purposes (e.g., "Workout Music", "Party Playlist").
         *          They provide quick access to frequently used track combinations.
         */
        struct WorkingSetInfo
        {
            WorkingSetId id;                      ///< Unique database identifier
            std::string name;                     ///< User-defined name for the working set
            Timestamp_t timestamp;                ///< Creation or last modification time
            int64_t numberOfTracks;               ///< Number of tracks in the working set
            Duration_t totalDuration;             ///< Combined duration of all tracks
            std::vector<SortOrderInfo> sortOrder; ///< Saved sort order for this working set
        };

    } // namespace database
} // namespace jucyaudio