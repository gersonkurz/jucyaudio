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
        const DataActions WorkingSetNodeActions{DataAction::EditWorkingSetMetadata, DataAction::CreateMix, DataAction::RunBpmAnalysis, DataAction::Delete};

        const DataActions WorkingSetRowActions{DataAction::Play,
            DataAction::CreateMix,
            DataAction::ShowDetails,
            DataAction::RemoveTracks,
            DataAction::RunBpmAnalysis};

        WorkingSetNode::WorkingSetNode(INavigationNode *parent, const WorkingSetInfo &workingSet)
            : LibraryNode{parent, workingSet.name, "Working Set", "Working Sets"},
              m_workingSetInfo{workingSet} // Call base constructor
        {
            m_queryArgs.workingSetId = workingSet.id;
        }
        
        void WorkingSetNode::rename(std::string_view newName)
        {
            m_workingSetInfo.name = newName;
            BaseNode::rename(newName);
        }

        WorkingSetId WorkingSetNode::getWorkingSetId() const
        {
            return m_queryArgs.workingSetId;
        }

        const DataActions &WorkingSetNode::getNodeActions() const
        {
            return WorkingSetNodeActions;
        }

        const DataActions &WorkingSetNode::getRowActions([[maybe_unused]] RowIndex_t rowIndex) const
        {
            return WorkingSetRowActions;
        }

        bool WorkingSetNode::deleteThisObject()
        {
            return theTrackLibrary.getWorkingSetManager().removeWorkingSet(m_queryArgs.workingSetId);
        }

        bool WorkingSetNode::removeObjects(const std::vector<ObjectId> &objectIds) const
        {
            return theTrackLibrary.getWorkingSetManager().removeTracksFromWorkingSet(m_queryArgs.workingSetId, objectIds);
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
