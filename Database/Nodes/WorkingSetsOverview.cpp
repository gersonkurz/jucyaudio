/*
  ==============================================================================

    WorkingSetsOverviewNode.cpp
    Created: 19 Jun 2025 3:43:11pm
    Author:  Gerson Kurz

  ==============================================================================
*/

#include <Database/Includes/INavigationNode.h>
#include <Database/Nodes/BaseNode.h>
#include <Database/Nodes/WorkingSetsOverview.h>
#include <Utils/AssortedUtils.h>

namespace jucyaudio
{
    // anonymous namespace: defines are only valid in this translation unit
    namespace
    {
        const ColumnIndex_t Column_Name{0};
        const ColumnIndex_t Column_TrackCount{1};
        const ColumnIndex_t Column_TotalLength{2};
    } // namespace

    namespace database
    {
        const DataActions WorkingSetsNodeActions{};
        const DataActions WorkingSetsRowActions{DataAction::Delete};
        const std::vector<DataColumn> WorkingSetsColumns = {
            DataColumn{Column_Name, "name", "Name", 200, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{Column_TrackCount, "track_count", "# Songs", 150, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{Column_TotalLength, "total_length", "Duration", 150, ColumnAlignment::Left, ColumnDataTypeHint::String}};

        const DataActions &TypedItemsOverview<WorkingSetInfo>::getNodeActions() const
        {
            return WorkingSetsNodeActions;
        }

        const DataActions &TypedItemsOverview<WorkingSetInfo>::getRowActions([[maybe_unused]] RowIndex_t rowIndex) const
        {
            return WorkingSetsRowActions;
        }

        bool TypedItemsOverview<WorkingSetInfo>::removeObject(const WorkingSetInfo &wsi) const
        {
            return theTrackLibrary.getWorkingSetManager().removeWorkingSet(wsi.id);
        }

        void TypedItemsOverview<WorkingSetInfo>::refreshCache(const TrackQueryArgs &args, std::vector<WorkingSetInfo> &data) const
        {
            data = theTrackLibrary.getWorkingSetManager().getWorkingSets(args);
        }

        const std::vector<DataColumn> &TypedItemsOverview<WorkingSetInfo>::getColumns() const
        {
            return WorkingSetsColumns;
        }

        std::string TypedItemsOverview<WorkingSetInfo>::getCellText(const WorkingSetInfo &wsi, ColumnIndex_t index) const
        {
            switch (index) // TODO: make these indices constants or an enum
            {
            case Column_Name: // Name
                return wsi.name;
            case Column_TrackCount: // Nr. of songs
                return std::to_string(wsi.track_count);
            case Column_TotalLength: // Duration
                return durationToString(wsi.total_duration);
            default:
                spdlog::warn("Invalid column index {} for WorkingSetsOverview", index);
                return "";
            }
        }
    } // namespace database
} // namespace jucyaudio
