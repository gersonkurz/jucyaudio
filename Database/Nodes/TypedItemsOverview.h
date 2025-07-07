/*
  ==============================================================================

    TypedTypedItemsOverview.h
    Created: 19 Jun 2025 6:02:35pm
    Author:  Gerson Kurz

  ==============================================================================
*/

#pragma once

#include <Database/Includes/INavigationNode.h>
#include <Database/Nodes/BaseNode.h>
#include <Database/Includes/Constants.h>

namespace jucyaudio
{
    namespace database
    {
        template <typename ITEM_TYPE> struct TypedItemsOverview
        {
            const DataActions &getNodeActions() const
            {
                return NoActionsPossible;
            }
            const DataActions &getRowActions(RowIndex_t rowIndex) const
            {
                return NoActionsPossible;
            }

            const std::vector<DataColumn> &getColumns() const
            {
                return NoColumnsPossible;
            }

            std::string getCellText(const ITEM_TYPE &wsi, ColumnIndex_t index) const
            {
                return "???";
            }
            bool removeObject(const ITEM_TYPE &wsi) const
            {
                return false;
            }
            void refreshCache(const TrackQueryArgs &args, std::vector<ITEM_TYPE> &data) const
            {
            }
        };
    } // namespace database
} // namespace jucyaudio