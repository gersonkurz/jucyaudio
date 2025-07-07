/*
  ==============================================================================

    WorkingSetsOverviewNode.h
    Created: 19 Jun 2025 3:43:11pm
    Author:  Gerson Kurz

  ==============================================================================
*/

#pragma once

#include <Database/Includes/INavigationNode.h>
#include <Database/Nodes/BaseNode.h>
#include <Database/TrackLibrary.h>
#include <Database/Nodes/TypedItemsOverview.h>

namespace jucyaudio
{
    namespace database
    {
        
        template <> 
        struct TypedItemsOverview<WorkingSetInfo>
        {
            const DataActions &getNodeActions() const;
            const DataActions &getRowActions(RowIndex_t rowIndex) const;
            const std::vector<DataColumn> &getColumns() const;

            std::string getCellText(const WorkingSetInfo &wsi, ColumnIndex_t index) const;
            bool removeObject(const WorkingSetInfo &wsi) const;
            void refreshCache(const TrackQueryArgs &args, std::vector<WorkingSetInfo> &data) const;
        };

    } // namespace database
} // namespace jucyaudio
