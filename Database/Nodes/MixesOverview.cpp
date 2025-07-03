/*
  ==============================================================================

    MixesOverviewNode.cpp
    Created: 19 Jun 2025 3:43:11pm
    Author:  Gerson Kurz

  ==============================================================================
*/

#include <Database/Includes/INavigationNode.h>
#include <Database/Nodes/BaseNode.h>
#include <Database/Nodes/MixesOverview.h>
#include <Utils/AssortedUtils.h>

namespace jucyaudio
{
    // anonymous namespace: defines are only valid in this translation unit
    namespace
    {
        const ColumnIndex_t Column_Name{0};
        const ColumnIndex_t Column_Created{1};
        const ColumnIndex_t Column_TrackCount{2};
        const ColumnIndex_t Column_TotalLength{3};
    } // namespace

    namespace database
    {
        const DataActions MixesNodeActions{};
        const DataActions MixesRowActions{DataAction::Delete};
        const std::vector<DataColumn> MixesColumns = {
            DataColumn{Column_Name, "name", "Name", 200, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{Column_Created, "created", "Created", 150, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{Column_TrackCount, "track_count", "# Songs", 150, ColumnAlignment::Left, ColumnDataTypeHint::String},
            DataColumn{Column_TotalLength, "total_length", "Duration", 150, ColumnAlignment::Left, ColumnDataTypeHint::String}};

        const DataActions &TypedItemsOverview<MixInfo>::getNodeActions() const
        {
            return MixesNodeActions;
        }

        const std::vector<DataColumn> &TypedItemsOverview<MixInfo>::getColumns() const
        {
            return MixesColumns;
        }

        const DataActions &TypedItemsOverview<MixInfo>::getRowActions([[maybe_unused]] RowIndex_t rowIndex) const
        {
            return MixesRowActions;
        }

        bool TypedItemsOverview<MixInfo>::removeObject(const TrackLibrary &library, const MixInfo &mix) const
        {
            return library.getMixManager().removeMix(mix.mixId);
        }

        void TypedItemsOverview<MixInfo>::refreshCache(const TrackLibrary &library, const TrackQueryArgs &args, std::vector<MixInfo> &data) const
        {
            data = library.getMixManager().getMixes(args);
        }

        std::string TypedItemsOverview<MixInfo>::getCellText(const MixInfo &mix, ColumnIndex_t index) const
        {
            switch (index) // TODO: make these indices constants or an enum
            {
            case Column_Name:
                return mix.name;
            case Column_Created:
                return timestampToString(mix.timestamp);
            case Column_TrackCount:
                return std::to_string(mix.numberOfTracks);
            case Column_TotalLength:
                return durationToString(mix.totalDuration);
            default:
                spdlog::warn("Invalid column index {} for MixesOverviewNode", index);
                return "";
            }
        }
    } // namespace database
} // namespace jucyaudio
