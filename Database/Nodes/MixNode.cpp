// Database/Nodes/MixNode.cpp
#include <Database/Nodes/MixNode.h>
#include <Utils/AssortedUtils.h>
#include <cassert>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        // Careful: these are actions for the *MixNode*, not the tracks shown in the mix.
        const DataActions MixNodeActions{DataAction::ShowMixEditor, DataAction::ShowTrackEditor, DataAction::Separator, DataAction::EditMixMetadata, DataAction::Delete, DataAction::ExportMix};
        MixNode::MixNode(INavigationNode *parent, const MixInfo &mixInfo)
            : LibraryNode{parent, mixInfo.name, "Mix", "Mixes"},
              m_mixInfo{mixInfo}
        {
            m_queryArgs.mixId = mixInfo.mixId;
        }
        void MixNode::rename(std::string_view newName)
        {
            m_mixInfo.name = newName;
            BaseNode::rename(newName);
        }
        const DataActions &MixNode::getNodeActions() const
        {
            return MixNodeActions;
        }

        bool MixNode::deleteThisObject()
        {
            return theTrackLibrary.getMixManager().removeMix(m_queryArgs.mixId);
        }

        bool MixNode::removeObjects(const std::vector<ObjectId> &objectIds) const
        {
            return theTrackLibrary.getMixManager().removeTracksFromMix(m_queryArgs.mixId, objectIds);
        }

        void MixNode::createChildren(INavigationNode *parent, std::vector<INavigationNode *> &children)
        {
            TrackQueryArgs args;
            const auto mixes{theTrackLibrary.getMixManager().getMixes(args)};
            for (const auto &mix : mixes)
            {
                children.emplace_back(new MixNode{parent, mix});
            }
        }
        
        const TrackInfo *MixNode::getTrackInfoForRow(RowIndex_t rowIndex) const
        {
            if (!m_bCacheInitialized)
            {
                refreshCache(false);
            }
            return m_mixProjectLoader.getTrackInfoForRow(rowIndex);
        }

        void MixNode::refreshCache(bool flushCache) const
        {
            // if the cache is invalid, or the rowIndex is out of bounds, we need to retrieve the rows
            const auto refreshCache = !m_bCacheInitialized || flushCache;
            if (refreshCache)
            {
                m_mixProjectLoader.loadMix(m_mixInfo.mixId);
                m_bCacheInitialized = true;
                // Clear the cached row count so it gets recalculated
                m_cachedRowCount = -1;
            }
        }
        
        DeletionAnalysisResult MixNode::analyzeDeletionRequest(const std::vector<RowIndex_t>& selectedRows) const
        {
            DeletionAnalysisResult result;
            
            // For a MixNode, all deletable items are tracks in the mix
            result.itemTypeSingular = "track";
            result.itemTypePlural = "tracks";
            
            // Ensure the mix is loaded
            if (!m_bCacheInitialized)
            {
                refreshCache(false);
            }
            
            for (const auto& rowIndex : selectedRows)
            {
                // A MixNode only contains tracks, which are always deletable
                const auto* trackInfo = m_mixProjectLoader.getTrackInfoForRow(rowIndex);
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
                    // This shouldn't happen in a MixNode, but handle it gracefully
                    result.nonDeletableCount++;
                }
            }
            
            return result;
        }
    } // namespace database
} // namespace jucyaudio
