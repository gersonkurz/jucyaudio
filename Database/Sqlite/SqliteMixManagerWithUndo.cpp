#include <Database/Sqlite/SqliteMixManagerWithUndo.h>
#include <spdlog/spdlog.h>
#include <unordered_map>

namespace jucyaudio
{
    namespace database
    {
        bool SqliteMixManagerWithUndo::createOrUpdateMix(MixInfo& mixInfo, std::vector<MixTrack>& tracks) const
        {
            spdlog::info("SqliteMixManagerWithUndo::createOrUpdateMix called for mix {} with {} tracks", 
                        mixInfo.mixId, tracks.size());
            
            // Begin a new operation for all changes in this call
            const auto operationId = m_undoManager.beginOperation();
            spdlog::info("Started operation {} for mix update", operationId);
            
            // For updates, get the current state for undo
            if (mixInfo.mixId != 0)
            {
                // Get current tracks before update
                auto oldTracks = m_wrappedManager.getMixTracks(mixInfo.mixId);
                
                // Record changes for each track
                std::unordered_map<TrackId, MixTrack> oldTrackMap;
                for (const auto& track : oldTracks)
                {
                    oldTrackMap[track.trackId] = track;
                }
                
                // Check for updates and new tracks
                for (const auto& newTrack : tracks)
                {
                    auto it = oldTrackMap.find(newTrack.trackId);
                    if (it != oldTrackMap.end())
                    {
                        // Track exists - record update
                        m_undoManager.recordMixTrackChange(mixInfo.mixId, newTrack.trackId,
                                                         &it->second, &newTrack, operationId);
                        oldTrackMap.erase(it);
                    }
                    else
                    {
                        // New track - record insert
                        m_undoManager.recordMixTrackChange(mixInfo.mixId, newTrack.trackId,
                                                         nullptr, &newTrack, operationId);
                    }
                }
                
                // Remaining tracks in oldTrackMap were deleted
                for (const auto& [trackId, oldTrack] : oldTrackMap)
                {
                    m_undoManager.recordMixTrackChange(mixInfo.mixId, trackId,
                                                     &oldTrack, nullptr, operationId);
                }
                
                // Record mix info change
                auto oldMixInfo = m_wrappedManager.getMix(mixInfo.mixId);
                spdlog::info("Got old mix info: name='{}', track_count={}", oldMixInfo.name, oldMixInfo.numberOfTracks);
                
                bool result = m_wrappedManager.createOrUpdateMix(mixInfo, tracks);
                if (result)
                {
                    spdlog::info("Mix update succeeded, recording undo for mix info change");
                    m_undoManager.recordMixInfoChange(mixInfo.mixId, &oldMixInfo, &mixInfo, operationId);
                }
                else
                {
                    spdlog::error("Mix update failed");
                }
                return result;
            }
            else
            {
                // New mix - just create it
                bool result = m_wrappedManager.createOrUpdateMix(mixInfo, tracks);
                if (result)
                {
                    // Record all tracks as inserts
                    for (const auto& track : tracks)
                    {
                        m_undoManager.recordMixTrackChange(mixInfo.mixId, track.trackId,
                                                         nullptr, &track, operationId);
                    }
                    // Record mix creation
                    m_undoManager.recordMixInfoChange(mixInfo.mixId, nullptr, &mixInfo, operationId);
                }
                return result;
            }
        }

        bool SqliteMixManagerWithUndo::removeMix(MixId mixId) const
        {
            const auto operationId = m_undoManager.beginOperation();
            
            // Get current state before deletion
            auto oldMixInfo = m_wrappedManager.getMix(mixId);
            auto oldTracks = m_wrappedManager.getMixTracks(mixId);
            
            bool result = m_wrappedManager.removeMix(mixId);
            if (result)
            {
                // Record all track deletions
                for (const auto& track : oldTracks)
                {
                    m_undoManager.recordMixTrackChange(mixId, track.trackId, &track, nullptr, operationId);
                }
                // Record mix deletion
                m_undoManager.recordMixInfoChange(mixId, &oldMixInfo, nullptr, operationId);
            }
            return result;
        }

        bool SqliteMixManagerWithUndo::removeMixes(const std::vector<MixId>& mixIds) const
        {
            // For batch operations, record each mix separately
            bool allSucceeded = true;
            for (const auto mixId : mixIds)
            {
                if (!removeMix(mixId))
                {
                    allSucceeded = false;
                }
            }
            return allSucceeded;
        }

        bool SqliteMixManagerWithUndo::renameMix(MixId mixId, std::string_view name) const
        {
            const auto operationId = m_undoManager.beginOperation();
            
            auto oldMixInfo = m_wrappedManager.getMix(mixId);
            bool result = m_wrappedManager.renameMix(mixId, name);
            if (result)
            {
                auto newMixInfo = m_wrappedManager.getMix(mixId);
                m_undoManager.recordMixInfoChange(mixId, &oldMixInfo, &newMixInfo, operationId);
            }
            return result;
        }

        bool SqliteMixManagerWithUndo::createAndSaveAutoMix(const std::vector<TrackInfo>& trackInfos,
                                                           MixInfo& mixInfo,
                                                           std::vector<MixTrack>& resultingTracks,
                                                           WorkingSetId source_ws_id,
                                                           const Duration_t defaultCrossfadeDuration) const
        {
            const auto operationId = m_undoManager.beginOperation();
            
            bool result = m_wrappedManager.createAndSaveAutoMix(trackInfos, mixInfo, resultingTracks,
                                                               source_ws_id, defaultCrossfadeDuration);
            if (result)
            {
                // Record all new tracks
                for (const auto& track : resultingTracks)
                {
                    m_undoManager.recordMixTrackChange(mixInfo.mixId, track.trackId, nullptr, &track, operationId);
                }
                // Record mix creation
                m_undoManager.recordMixInfoChange(mixInfo.mixId, nullptr, &mixInfo, operationId);
            }
            return result;
        }

        bool SqliteMixManagerWithUndo::removeTrackFromMix(MixId mixId, TrackId trackId) const
        {
            const auto operationId = m_undoManager.beginOperation();
            
            // Get the track before deletion
            auto tracks = m_wrappedManager.getMixTracks(mixId);
            const MixTrack* oldTrack = nullptr;
            for (const auto& track : tracks)
            {
                if (track.trackId == trackId)
                {
                    oldTrack = &track;
                    break;
                }
            }
            
            if (!oldTrack)
            {
                spdlog::warn("Track {} not found in mix {} for undo recording", trackId, mixId);
                return false;
            }
            
            MixTrack trackCopy = *oldTrack; // Make a copy
            bool result = m_wrappedManager.removeTrackFromMix(mixId, trackId);
            if (result)
            {
                m_undoManager.recordMixTrackChange(mixId, trackId, &trackCopy, nullptr, operationId);
            }
            return result;
        }

        bool SqliteMixManagerWithUndo::removeTracksFromMix(MixId mixId, const std::vector<TrackId>& trackIds) const
        {
            const auto operationId = m_undoManager.beginOperation();
            
            // Get all tracks before deletion
            auto allTracks = m_wrappedManager.getMixTracks(mixId);
            std::unordered_map<TrackId, MixTrack> trackMap;
            for (const auto& track : allTracks)
            {
                trackMap[track.trackId] = track;
            }
            
            bool result = m_wrappedManager.removeTracksFromMix(mixId, trackIds);
            if (result)
            {
                // Record deletions for each track
                for (const auto trackId : trackIds)
                {
                    auto it = trackMap.find(trackId);
                    if (it != trackMap.end())
                    {
                        m_undoManager.recordMixTrackChange(mixId, trackId, &it->second, nullptr, operationId);
                    }
                }
            }
            return result;
        }

        bool SqliteMixManagerWithUndo::finalizeMix(MixId mixId) const
        {
            const auto operationId = m_undoManager.beginOperation();
            
            // Finalization doesn't change mix content, just status
            auto oldMixInfo = m_wrappedManager.getMix(mixId);
            bool result = m_wrappedManager.finalizeMix(mixId);
            if (result)
            {
                auto newMixInfo = m_wrappedManager.getMix(mixId);
                m_undoManager.recordMixInfoChange(mixId, &oldMixInfo, &newMixInfo, operationId);
            }
            return result;
        }
        
        bool SqliteMixManagerWithUndo::clearMixWorkingSetId(MixId mixId) const
        {
            const auto operationId = m_undoManager.beginOperation();
            
            // Record the change to working_set_id
            auto oldMixInfo = m_wrappedManager.getMix(mixId);
            bool result = m_wrappedManager.clearMixWorkingSetId(mixId);
            if (result)
            {
                auto newMixInfo = m_wrappedManager.getMix(mixId);
                m_undoManager.recordMixInfoChange(mixId, &oldMixInfo, &newMixInfo, operationId);
            }
            return result;
        }

        bool SqliteMixManagerWithUndo::updateMixTrack(MixId mixId, const MixTrack& updatedTrack) const
        {
            const auto operationId = m_undoManager.beginOperation();

            // Get the old track data for undo recording
            auto oldTracks = m_wrappedManager.getMixTracks(mixId);
            std::optional<MixTrack> oldTrackData;

            for (const auto& track : oldTracks)
            {
                if (track.trackId == updatedTrack.trackId)
                {
                    oldTrackData = track;
                    break;
                }
            }

            // Delegate the actual update to the wrapped manager
            bool result = m_wrappedManager.updateMixTrack(mixId, updatedTrack);

            if (result)
            {
                // Only record undo if the track actually existed and changed
                if (oldTrackData.has_value())
                {
                    // Compare old and new track data to avoid recording unnecessary undo operations
                    bool changed = false;
                    if (oldTrackData->gainAdjustment != updatedTrack.gainAdjustment)
                    {
                        changed = true;
                    }
                    if (!changed && oldTrackData->envelopePoints.size() != updatedTrack.envelopePoints.size())
                    {
                        changed = true;
                    }
                    if (!changed)
                    {
                        for (size_t i = 0; i < oldTrackData->envelopePoints.size(); ++i)
                        {
                            if (oldTrackData->envelopePoints[i].time != updatedTrack.envelopePoints[i].time ||
                                oldTrackData->envelopePoints[i].volume != updatedTrack.envelopePoints[i].volume)
                            {
                                changed = true;
                                break;
                            }
                        }
                    }
                    if (!changed && (oldTrackData->cueStart != updatedTrack.cueStart ||
                                     oldTrackData->cueEnd != updatedTrack.cueEnd ||
                                     oldTrackData->attachFrom != updatedTrack.attachFrom ||
                                     oldTrackData->attachTo != updatedTrack.attachTo))
                    {
                        changed = true;
                    }


                    if (changed)
                    {
                        m_undoManager.recordMixTrackChange(mixId, updatedTrack.trackId, &oldTrackData.value(), &updatedTrack, operationId);
                    }
                    else
                    {
                        spdlog::info("MixTrack {} in mix {} did not change, no undo recorded.", updatedTrack.trackId, mixId);
                    }
                }
                else
                {
                    // If oldTrackData was not found, it means this is a new track being inserted
                    // This case should ideally be handled by createOrUpdateMix or a dedicated insert method
                    // For now, log a warning if this path is unexpectedly hit for an update
                    spdlog::warn("Attempted to update non-existent MixTrack {} in mix {}. No undo recorded for insert.", updatedTrack.trackId, mixId);
                }
            }
            return result;
        }

    } // namespace database
} // namespace jucyaudio