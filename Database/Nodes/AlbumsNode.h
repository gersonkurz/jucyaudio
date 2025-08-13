#pragma once

#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/AlbumInfo.h>
#include <Database/TrackLibrary.h>
#include <Database/Nodes/BaseNode.h>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        class AlbumsNode : public BaseNode
        {
        public:
            AlbumsNode(INavigationNode *root,
                const std::string &name,
                std::string_view typeNameForSingleObject,
                std::string_view typeNameForMultipleObjects);
            ~AlbumsNode() override;

        protected:
            bool prepareToShowData() override;
            bool setSortOrder(const std::vector<SortOrderInfo> &sortOrders) override;
            
        private:
            // INavigationNode interface
            const std::vector<DataColumn> &getColumns() const override;
            bool getNumberOfRows(int64_t &outCount) const override;
            std::string getCellText(RowIndex_t rowIndex, ColumnIndex_t index) const override;
            const TrackInfo *getTrackInfoForRow(RowIndex_t rowIndex) const override;
            int64_t getObjectIdForRow(RowIndex_t rowIndex) const override;
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
            
        public:
            // Album-specific methods
            const AlbumInfo *getAlbumInfoForRow(RowIndex_t rowIndex) const;
            FolderId getFolderIdForRow(RowIndex_t rowIndex) const;
            
        private:
            mutable std::vector<AlbumInfo> m_albums;
            mutable int64_t m_cachedRowCount{-1};
            mutable bool m_bCacheInitialized{false};
            std::vector<std::string> m_searchTerms;
            std::vector<SortOrderInfo> m_sortOrders;
        };

    } // namespace database
} // namespace jucyaudio