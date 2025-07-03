#pragma once

#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/TrackInfo.h>

namespace jucyaudio
{
    namespace database
    {
        /// @brief A DataColumn represents a column in a navigation node.
        /// @example you have a list of two possible datacolumns defined for the library: album (index 0) and artist (1)
        /// In the UI, you want to show only the artist. so you have a vector of DataColumnWithIndex, with one item, with 
        struct DataColumnWithIndex
        {
            /// @brief Pointer to the actual DataColumn.
            DataColumn *column{nullptr}; 
        };

    } // namespace database

    namespace ui
    {
        namespace columns
        {
            // get actual columns to show for view
            bool get(database::INavigationNode* node, std::vector<database::DataColumnWithIndex> &columns);

            void cleanup();

        } // namespace columns
    } // namespace ui
} // namespace jucyaudio
