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
        const DataActions WorkingSetNodeActions{DataAction::EditWorkingSetMetadata,
            DataAction::CreateMix,
            DataAction::RunBpmAnalysis,
            DataAction::Delete,
            DataAction::RemoveDuplicates};

        const DataActions WorkingSetRowActions{
            DataAction::Play,
            DataAction::CreateMix,
            DataAction::ShowDetails,
            DataAction::CopyToClipboard,
            DataAction::ShuffleSelection,
            DataAction::RemoveTracks,
            DataAction::RunBpmAnalysis,
            DataAction::CheckFilesOnDisk};

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
            // The data view re-applies the saved sort through here every time this node is shown. That
            // must not renumber the set, or showing it would undo every shuffle. A header click always
            // asks for something else - another column, or the same one the other way round.
            if (sortOrders == m_workingSetInfo.sortOrder)
            {
                return true;
            }

            // The set's stored positions are what it is displayed by, so a sort is applied by renumbering
            // them; the query itself ignores sortBy for a working set.
            if (!theTrackLibrary.getWorkingSetManager().applySortOrder(m_workingSetInfo.id, sortOrders))
            {
                spdlog::error("Failed to apply sort order to working set {}", m_workingSetInfo.id);
                return false;
            }
            m_workingSetInfo.sortOrder = sortOrders;
            spdlog::info("Sorted working set {} ({})", m_workingSetInfo.id, m_workingSetInfo.name);

            // Flushes the rows this node is holding, which are in the old order.
            return LibraryNode::setSortOrder(sortOrders);
        }

        std::vector<SortOrderInfo> WorkingSetNode::getCurrentSortOrder() const
        {
            // Return the saved sort order from our working set info
            return m_workingSetInfo.sortOrder;
        }

        DeletionAnalysisResult WorkingSetNode::analyzeDeletionRequest(const std::vector<RowIndex_t>& selectedRows) const
        {
            DeletionAnalysisResult result;
            
            // For a WorkingSetNode, all deletable items are tracks
            result.itemTypeSingular = "track";
            result.itemTypePlural = "tracks";
            
            for (const auto& rowIndex : selectedRows)
            {
                // A WorkingSetNode only contains tracks, which are always deletable
                const auto* trackInfo = getTrackInfoForRow(rowIndex);
                if (trackInfo)
                {
                    result.deletableObjectIds.push_back(trackInfo->trackId);
                    
                    // If this is the only item being deleted, store its name
                    if (selectedRows.size() == 1)
                    {
                        // Use artist - title format for track name
                        if (!trackInfo->artist_name.empty() && !trackInfo->title.empty())
                        {
                            result.singleItemName = trackInfo->artist_name + " - " + trackInfo->title;
                        }
                        else if (!trackInfo->title.empty())
                        {
                            result.singleItemName = trackInfo->title;
                        }
                        else
                        {
                            result.singleItemName = trackInfo->filename;
                        }
                    }
                }
                else
                {
                    // This shouldn't happen in a WorkingSetNode, but handle it gracefully
                    result.nonDeletableCount++;
                }
            }
            
            return result;
        }
                
        TrackInfosForOperationResult WorkingSetNode::getTrackInfosForOperation(const std::vector<RowIndex_t>& selectedRows) const
        {
            TrackInfosForOperationResult result;
            
            // A WorkingSetNode only contains tracks, all of which are valid for operations
            for (const auto& rowIndex : selectedRows)
            {
                const auto* trackInfo = getTrackInfoForRow(rowIndex);
                if (trackInfo)
                {
                    result.trackInfos.push_back(*trackInfo);
                }
                else
                {
                    // This shouldn't happen in a WorkingSetNode, but handle it gracefully
                    result.nonApplicableCount++;
                }
            }
            
            return result;
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
