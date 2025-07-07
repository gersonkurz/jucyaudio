// Engine/LibraryNode.h
#pragma once

#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/TrackQueryArgs.h>
#include <algorithm> // For std::generate_n
#include <atomic>    // For unique ID generation
#include <random>    // For randomized data
#include <string>
#include <vector>
#ifdef USE_REFCOUNT_DEBUGGING
#include <mutex>
#include <unordered_set>
#endif


namespace jucyaudio
{
    namespace database
    {
        extern const std::vector<DataColumn> NoColumnsPossible;
        extern const DataActions NoActionsPossible;

        class BaseNode : public INavigationNode
        {
        public:
            BaseNode(INavigationNode *parent, std::string_view name);
            ~BaseNode() override;

            // IRefCounted interface
            void retain(REFCOUNT_DEBUG_SPEC) const override final;
            void release(REFCOUNT_DEBUG_SPEC) const override final;
            void clear() override;
            const std::string &getName() const override final;

        protected:
            // INavigationNode interface
            INavigationNode *getParent() const override final;
            void refreshChildren() override;

            bool setSortOrder(const std::vector<SortOrderInfo> &sortOrders) override;
            std::vector<SortOrderInfo> getCurrentSortOrder() const override;
            bool setSearchTerms(const std::vector<std::string> &searchTerms) override;
            std::vector<std::string> getCurrentSearchTerms() const override;
            const DataActions &getNodeActions() const override;
            const DataActions &getRowActions(RowIndex_t rowIndex) const override;
            std::string getCellText(RowIndex_t rowIndex, ColumnIndex_t index) const override;
            const TrackInfo *getTrackInfoForRow(RowIndex_t rowIndex) const override;
            bool getNumberOfRows(int64_t &outCount) const override;
            bool prepareToShowData() override;
            void dataNoLongerShowing() override;
            const std::vector<DataColumn> &getColumns() const override;
            const TrackQueryArgs *getQueryArgs() const override;
            void removeObjectAtRow(RowIndex_t rowIndex) override;
            int64_t getUniqueId() const override;
            INavigationNode *get(const std::string &name) const override;
            INavigationNode *get(int64_t uniqueId) const override;
            void refreshCache(bool flushCache = false) const override
            {
            }

        private:
            INavigationNode *const m_parent;
            const std::string m_name;
            mutable std::atomic<int32_t> m_refCount;

         protected:
            friend class RootNode; // Allow RootNode to access m_children
            mutable std::vector<INavigationNode *> m_children;
        };

        // helper function to get the path of nodes
        std::vector<INavigationNode*> getNodePath(INavigationNode *targetNode);

#ifdef USE_REFCOUNT_DEBUGGING
        extern std::unordered_set<BaseNode *> theBaseNodes;
#endif
    } // namespace database
} // namespace jucyaudio
