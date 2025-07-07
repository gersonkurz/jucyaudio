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
#include <Database/Nodes/TypedContainerNode.h>
#include <Database/Nodes/TypedItemsOverview.h>

namespace jucyaudio
{
    namespace database
    {
        template <typename ITEM_TYPE, typename NODE_TYPE>
        class TypedOverviewNode final : public TypedContainerNode<NODE_TYPE>
        {
        public:
            explicit TypedOverviewNode<ITEM_TYPE, NODE_TYPE>(
                INavigationNode *root, TrackLibrary &library,
                std::string_view name,
                TypedContainerNode<NODE_TYPE>::ClientCreationMethod
                    clientCreationMethod)
                : TypedContainerNode<NODE_TYPE>{root, library, name,
                                                clientCreationMethod},
                  m_library2{library}
            {
            }

            bool setSortOrder(
                const std::vector<SortOrderInfo> &sortOrders) override
            {
                m_queryArgs.sortBy = sortOrders;
                m_bCacheInitialized = false;
                return true;
            }

            std::vector<SortOrderInfo> getCurrentSortOrder() const override
            {
                return m_queryArgs.sortBy;
            }

            bool setSearchTerms(
                const std::vector<std::string> &searchTerms) override
            {
                m_queryArgs.searchTerms = searchTerms;
                m_bCacheInitialized = false;
                return true;
            }

            std::vector<std::string> getCurrentSearchTerms() const override
            {
                return m_queryArgs.searchTerms;
            }

            const DataActions &getNodeActions() const override
            {
                return m_overview.getNodeActions();
            }

            const DataActions &getRowActions(RowIndex_t rowIndex) const override
            {
                return m_overview.getRowActions(rowIndex);
            }

            std::string getCellText(RowIndex_t rowIndex,
                                    ColumnIndex_t index) const override
            {
                // TODO: we should have some sort of abstract track info to
                // return here. We're really calling it only to refresh the
                // cache if needed
                refreshCache();
                if (rowIndex >= m_objects.size())
                    return "???";
                return m_overview.getCellText(m_objects[rowIndex], index);
            }

            bool getNumberOfRows(int64_t &outCount) const override
            {
                refreshCache();
                outCount = m_objects.size();
                return true;
            }

            const std::vector<DataColumn> &getColumns() const override
            {
                return m_overview.getColumns();
            }

            const TrackQueryArgs *getQueryArgs() const override
            {
                return &m_queryArgs;
            }

            void removeObjectAtRow(RowIndex_t rowIndex) override
            {
                spdlog::info("Removing object at row index: {}", rowIndex);
                if (rowIndex < m_objects.size())
                {
                    const auto &ws = m_objects[rowIndex];
                    if (!m_overview.removeObject(m_library2, ws))
                    {
                        spdlog::error(
                            "Failed to remove object at row index: {}",
                            rowIndex);
                    }
                    else
                    {
                        spdlog::info(
                            "Successfully removed object at row index: {}",
                            rowIndex);
                        m_bCacheInitialized = false;
                    }
                }
            }

            bool prepareToShowData() override
            {
                if (m_bCacheInitialized || m_objects.empty())
                {
                    spdlog::info(
                        "TON: Preparing to show data for TypedOverviewNode");
                    m_bCacheInitialized = false; // Reset cache state
                    refreshCache();
                }
                return true;
            }

            void dataNoLongerShowing() override
            {
                if (m_bCacheInitialized)
                {
                    spdlog::info(
                        "TON: Data no longer showing for TypedOverviewNode");
                    m_bCacheInitialized = false;
                    m_objects.clear(); // Clear the cache when data is no longer
                                       // shown
                }
            }

            void refreshCache(bool flushCache = false) const override
            {
                // if the cache is invalid, or the rowIndex is out of bounds, we
                // need to retrieve the rows
                if (flushCache || !m_bCacheInitialized)
                {
                    m_overview.refreshCache(m_library2, m_queryArgs, m_objects);
                    m_bCacheInitialized = true;
                }
            }



            void refreshData()
            {
                spdlog::info("Refreshing data for overview node: {}",
                             this->getName().toStdString());

                // 1. Refresh/invalidate list view data cache
                refreshCache(true); // true to force a flush and re-fetch

                // If this node is currently the one displayed in DataView,
                // MainComponent needs to trigger DataView update. This could be
                // done by MainComponent checking: if (m_currentSelectedDataNode
                // == overviewNodeInstance) { m_dataView.updateContent(); } Or,
                // TypedOverviewNode could have a callback/signal that it emits
                // here. For now, let's assume MainComponent handles the
                // DataView update if needed.

                // 2. Refresh tree children (if this node is also a tree parent)
                //    This calls the refreshChildren method we discussed
                //    earlier, which intelligently updates or rebuilds its
                //    m_children (the INavigationNode models for the tree).
                TypedContainerNode<
                    NODE_TYPE>::refreshChildren(); // Call base class or its own
                                                   // impl if overridden

            }

        private:
            TrackLibrary &m_library2;
            mutable TrackQueryArgs m_queryArgs;
            mutable std::vector<ITEM_TYPE> m_objects;
            mutable bool m_bCacheInitialized{false};
            TypedItemsOverview<ITEM_TYPE> m_overview;
        };
    } // namespace database
} // namespace jucyaudio
