// Database/Nodes/WorkingSetNode.cpp
#include <Database/Nodes/WorkingSetNode.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>
#include <cassert>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        const DataActions WorkingSetNodeActions{DataAction::EditMetadata, DataAction::CreateMix, DataAction::RunBpmAnalysis, DataAction::RemoveWorkingSet};

        const DataActions WorkingSetRowActions{DataAction::Play,
            DataAction::CreateMix,
            DataAction::ShowDetails,
            DataAction::EditMetadata,
            DataAction::Delete,
            DataAction::RunBpmAnalysis};

        WorkingSetNode::WorkingSetNode(INavigationNode *parent, const WorkingSetInfo &workingSet)
            : LibraryNode{parent, workingSet.name, NodeType::WorkingSet},
              m_workingSetInfo{workingSet} // Call base constructor
        {
            m_queryArgs.workingSetId = workingSet.id;
        }

        const DataActions &WorkingSetNode::getNodeActions() const
        {
            return WorkingSetNodeActions;
        }

        const DataActions &WorkingSetNode::getRowActions([[maybe_unused]] RowIndex_t rowIndex) const
        {
            return WorkingSetRowActions;
        }

        bool WorkingSetNode::setSortOrder(const std::vector<SortOrderInfo> &sortOrders)
        {
            // First call the base class implementation to update the query args
            if (!LibraryNode::setSortOrder(sortOrders))
            {
                return false;
            }

            // Update our local copy
            m_workingSetInfo.sortOrder = sortOrders;

            // Persist to database
            if (!theTrackLibrary.getWorkingSetManager().updateSortOrder(m_workingSetInfo.id, sortOrders))
            {
                spdlog::error("Failed to persist sort order for working set {}", m_workingSetInfo.id);
                return false;
            }

            spdlog::info("Persisted sort order for working set {} ({})", m_workingSetInfo.id, m_workingSetInfo.name);
            return true;
        }

        std::vector<SortOrderInfo> WorkingSetNode::getCurrentSortOrder() const
        {
            // Return the saved sort order from our working set info
            return m_workingSetInfo.sortOrder;
        }

        void WorkingSetNode::createChildren(INavigationNode *parent, std::vector<INavigationNode *> &children)
        {
            TrackQueryArgs args{};
            const auto workingSets{theTrackLibrary.getWorkingSetManager().getWorkingSets(args)};
            for (const auto &workingSet : workingSets)
            {
                children.emplace_back(new WorkingSetNode{parent, workingSet});
            }
        }

    } // namespace database
} // namespace jucyaudio
