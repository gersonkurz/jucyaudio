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
        struct TypedItemsOverview<MixInfo>
        {
            const DataActions &getNodeActions() const;
            const DataActions &getRowActions(RowIndex_t rowIndex) const;
            const std::vector<DataColumn> &getColumns() const;

            std::string getCellText(const MixInfo &wsi, ColumnIndex_t index) const;
            bool removeObject(const TrackLibrary &library, const MixInfo &wsi) const;
            void refreshCache(const TrackLibrary &library, const TrackQueryArgs &args, std::vector<MixInfo> &data) const;
        };

    } // namespace database
} // namespace jucyaudio
