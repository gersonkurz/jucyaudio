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
            LibraryNode(INavigationNode *root, TrackLibrary &library, const std::string& name = "");
            ~LibraryNode() override;

        private:
            
            // INavigationNode interface
            bool getChildren(std::vector<INavigationNode *> &outChildren) override;
            bool hasChildren() const override;
            const std::vector<DataColumn> &getColumns() const override;
            bool getNumberOfRows(int64_t &outCount) const override;
            std::string getCellText(RowIndex_t rowIndex, ColumnIndex_t index) const override;
            const TrackInfo *getTrackInfoForRow(RowIndex_t rowIndex) const override;
            bool prepareToShowData() override;
            void dataNoLongerShowing() override;
            const DataActions &getNodeActions() const override;
            const DataActions &getRowActions(RowIndex_t rowIndex) const override;
            bool setSearchTerms(const std::vector<std::string> &searchTerms) override;
            std::vector<std::string> getCurrentSearchTerms() const override;
            bool setSortOrder(const std::vector<SortOrderInfo> &sortOrders) override;
            std::vector<SortOrderInfo> getCurrentSortOrder() const override;
            const TrackQueryArgs *getQueryArgs() const override;

        private:
            mutable std::vector<TrackInfo> m_tracks;
            mutable bool m_bCacheInitialized{false};

        protected:
            TrackLibrary &m_library;
            mutable TrackQueryArgs m_queryArgs;
        };

    } // namespace database
} // namespace jucyaudio