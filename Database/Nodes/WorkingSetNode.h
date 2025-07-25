#pragma once

// Database/Nodes/WorkingSetNode.h
#include <Database/Nodes/LibraryNode.h>
#include <filesystem>

namespace jucyaudio
{
    namespace database
    {
        class WorkingSetNode final : public LibraryNode
        {
        public:
            explicit WorkingSetNode(INavigationNode *parent, const WorkingSetInfo& workingSet);
            ~WorkingSetNode() override = default;

            const auto& getWorkingSetInfo() const
            {
                return m_workingSetInfo;
            }

            int64_t getUniqueId() const override
            {
                return m_queryArgs.workingSetId;
            }

            const DataActions &getNodeActions() const override;
            const DataActions &getRowActions(RowIndex_t rowIndex) const;
            bool deleteThisObject() override; 
            bool removeTracks(const std::vector<TrackId> &trackIds) const override;
            NodeType getNodeType() const override
            {
                return NodeType::WorkingSet;
            }

            void rename(std::string_view newName)
            {
                m_workingSetInfo.name = newName;
                BaseNode::rename(newName);
            }
            
            // Override to persist sort order to database
            bool setSortOrder(const std::vector<SortOrderInfo> &sortOrders) override;
            std::vector<SortOrderInfo> getCurrentSortOrder() const override;

            static void createChildren(INavigationNode *parent, std::vector<INavigationNode *> &children);

        private:
            WorkingSetInfo m_workingSetInfo;
        };
    } // namespace database
} // namespace jucyaudio
