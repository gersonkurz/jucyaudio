// Engine/LibraryNode.h
#pragma once

#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/TrackLibrary.h>
#include <Database/Nodes/BaseNode.h>
#include <algorithm>         // For std::generate_n
#include <atomic>            // For unique ID generation
#include <random>            // For randomized data
#include <string>
#include <vector>


namespace jucyaudio
{
    namespace database
    {
        class LibraryNode : public BaseNode
        {
        public:
            LibraryNode(INavigationNode *root,
                const std::string &name,
                std::string_view typeNameForSingleObject,
                std::string_view typeNameForMultipleObjects);
            ~LibraryNode() override;

        protected:
            bool prepareToShowData() override;
            bool setSortOrder(const std::vector<SortOrderInfo> &sortOrders) override;
            bool getAggregateStats(AggregateStats& outStats) const override;
            const TrackInfo *getTrackInfoForRow(RowIndex_t rowIndex) const override;
            CellRenderInfo getCellRenderInfo(RowIndex_t rowIndex, ColumnIndex_t columnIndex) const override;

        private:
            
            // INavigationNode interface
            const std::vector<DataColumn> &getColumns() const override;
            bool getNumberOfRows(int64_t &outCount) const override;
            std::string getCellText(RowIndex_t rowIndex, ColumnIndex_t index) const override;
            
            ObjectId getObjectIdForRow(RowIndex_t rowIndex) const override;
            void dataNoLongerShowing() override;
            const DataActions &getNodeActions() const override;
            const DataActions &getRowActions(RowIndex_t rowIndex) const override;
            bool setSearchTerms(const std::vector<std::string> &searchTerms) override;
            std::vector<std::string> getCurrentSearchTerms() const override;
            std::vector<SortOrderInfo> getCurrentSortOrder() const override;
            const TrackQueryArgs *getQueryArgs() const override;
            void refreshCache(bool flushCache = false) const override;
            std::vector<TrackId> getAllTrackIds() const override;
            bool canExpand() override;
            bool expand(std::vector<INavigationNode *> &outChildren) override;

        private:
            mutable std::vector<TrackInfo> m_tracks;

        protected:
            mutable bool m_bCacheInitialized{false};
            mutable TrackQueryArgs m_queryArgs;
            mutable int64_t m_cachedRowCount{-1};
        };

    } // namespace database
} // namespace jucyaudio