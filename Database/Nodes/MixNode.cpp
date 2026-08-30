// Database/Nodes/MixNode.cpp
#include <Database/Nodes/MixNode.h>
#include <Utils/AssortedUtils.h>
#include <cassert>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        const DataActions MixTrackRowActions{
            DataAction::Play,
            DataAction::ShowDetails,
            DataAction::CopyToClipboard,
            DataAction::Delete
        };

        MixNode::MixNode(INavigationNode *parent, const MixInfo &mixInfo)
            : LibraryNode{parent, mixInfo.name, "Mix", "Mixes"},
              m_mixInfo{mixInfo}
        {
            m_queryArgs.mixId = mixInfo.mixId;
            buildDynamicActions();
        }
        
        void MixNode::buildDynamicActions()
        {
            // Build dynamic action list based on mix status
            m_dynamicActions.clear();
            m_dynamicActions.push_back(DataAction::ShowMixEditor);
            m_dynamicActions.push_back(DataAction::ShowTrackEditor);
            m_dynamicActions.push_back(DataAction::Separator);
            
            // Add unlock option if mix is exported (has export folder)
            if (m_mixInfo.exportFolder.has_value() && !m_mixInfo.exportFolder->empty())
            {
                m_dynamicActions.push_back(DataAction::UnlockMixForEditing);
                m_dynamicActions.push_back(DataAction::Separator);
            }
            
            m_dynamicActions.push_back(DataAction::EditMixMetadata);
            m_dynamicActions.push_back(DataAction::Delete);
            m_dynamicActions.push_back(DataAction::ExportMix);
        }
        void MixNode::rename(std::string_view newName)
        {
            m_mixInfo.name = newName;
            BaseNode::rename(newName);
        }
        const DataActions &MixNode::getNodeActions() const
        {
            return m_dynamicActions;
        }

        bool MixNode::deleteThisObject()
        {
            return theTrackLibrary.getMixManager().removeMix(m_queryArgs.mixId);
        }

        bool MixNode::removeObjects(const std::vector<ObjectId> &objectIds) const
        {
            const bool success = theTrackLibrary.getMixManager().removeTracksFromMix(m_queryArgs.mixId, objectIds);

            if (success)
            {
                // Update cached metadata after removing tracks
                // Force reload to recalculate track count and total duration
                if (m_bCacheInitialized)
                {
                    // Reload the mix to get accurate track count and duration
                    const_cast<MixNode*>(this)->refreshCache(true); // true = flush and reload
                }
            }

            return success;
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

        const DataActions &MixNode::getRowActions([[maybe_unused]] RowIndex_t rowIndex) const
        {
            return MixTrackRowActions;
        }
        
        const TrackInfo *MixNode::getTrackInfoForRow(RowIndex_t rowIndex) const
        {
            if (!m_bCacheInitialized)
            {
                refreshCache(false);
            }
            return m_mixProjectLoader.getTrackInfoForRow(rowIndex);
        }

        bool MixNode::getNumberOfRows(int64_t &outCount) const
        {
            if (!m_bCacheInitialized)
            {
                refreshCache(false);
            }
            outCount = static_cast<int64_t>(m_mixProjectLoader.getMixTracks().size());

            // False when the load failed, so a caller holding this interface can tell "this mix has
            // no tracks" from "nobody knows what this mix has". They read the same otherwise, and
            // only one of them is safe to act on.
            return m_mixProjectLoader.isLoaded();
        }

        void MixNode::refreshCache(bool flushCache) const
        {
            // if the cache is invalid, or the rowIndex is out of bounds, we need to retrieve the rows
            const auto refreshCache = !m_bCacheInitialized || flushCache;
            if (refreshCache)
            {
                // The result matters. loadMix publishes nothing unless it succeeds, so a failure leaves
                // the loader still holding the previous mix - which keeps existing pointers into it
                // valid, but means what it holds no longer describes the database. Anything that writes
                // has to know the difference.
                if (!m_mixProjectLoader.loadMix(m_mixInfo.mixId))
                {
                    spdlog::error("[MixNode] Mix {} could not be loaded; its cache holds nothing.", m_mixInfo.mixId);
                }

                // Marked initialised either way, so callers get one consistent answer rather than a
                // fresh attempt on every access. isCacheLoaded() is what says whether it worked.
                m_bCacheInitialized = true;
                // Clear the cached row count so it gets recalculated
                m_cachedRowCount = -1;
            }
        }

        void MixNode::refreshMixInfo()
        {
            // Refresh the mix info from database
            const auto& mixManager = database::theTrackLibrary.getMixManager();
            m_mixInfo = mixManager.getMix(m_mixInfo.mixId);

            // Rebuild dynamic actions since they depend on mix status
            buildDynamicActions();

            spdlog::info("[MixNode] Refreshed mix info for mix {}, exportFolder: {}",
                        m_mixInfo.mixId,
                        m_mixInfo.exportFolder.has_value() ? *m_mixInfo.exportFolder : "NULL");
        }

        void MixNode::updateSummaryMetadata(int trackCount, Duration_t totalDuration)
        {
            // Update only the summary statistics without touching track data or triggering cache refresh
            m_mixInfo.numberOfTracks = trackCount;
            m_mixInfo.totalDuration = totalDuration;

            spdlog::debug("[MixNode] Updated summary metadata for mix {}: {} tracks, {} total duration",
                        m_mixInfo.mixId, trackCount, totalDuration.count());
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
        
        TrackInfosForOperationResult MixNode::getTrackInfosForOperation(const std::vector<RowIndex_t>& selectedRows) const
        {
            TrackInfosForOperationResult result;
            
            // Ensure the mix is loaded
            if (!m_bCacheInitialized)
            {
                refreshCache(false);
            }
            
            // A MixNode only contains tracks, all of which are valid for operations
            for (const auto& rowIndex : selectedRows)
            {
                const auto* trackInfo = m_mixProjectLoader.getTrackInfoForRow(rowIndex);
                if (trackInfo)
                {
                    result.trackInfos.push_back(*trackInfo);
                }
                else
                {
                    // This shouldn't happen in a MixNode, but handle it gracefully
                    result.nonApplicableCount++;
                }
            }
            
            return result;
        }
    } // namespace database
} // namespace jucyaudio
