#include <Audio/MixProjectLoader.h>
#include <Utils/StringWriter.h>
#include <Utils/AssortedUtils.h>
#include <algorithm>
#include <unordered_set>

namespace jucyaudio
{
    namespace audio
    {
        // Constructor loads all necessary data from the database.
        MixProjectLoader::MixProjectLoader()
            : m_mixId{0}
        {
        }

        void MixProjectLoader::loadMix(MixId mixId)
        {
            spdlog::debug("MixProjectLoader: Loading mix with ID {}", mixId);
            m_mixId = mixId;
            m_mixInfo = theTrackLibrary.getMixManager().getMix(m_mixId);
            m_mixTracks = theTrackLibrary.getMixManager().getMixTracks(m_mixId);
            spdlog::info("MixProjectLoader: Loaded {} tracks for mix ID {}", m_mixTracks.size(), m_mixId);
            m_trackInfos = theTrackLibrary.getTracks(getMixTrackQueryArgs(m_mixId));
            spdlog::info("MixProjectLoader: Loaded {} track infos for mix ID {}", m_trackInfos.size(), m_mixId);

            m_trackInfosMap.clear();

            for (const auto &ti : m_trackInfos)
            {
                m_trackInfosMap[ti.trackId] = &ti;
            }
            int index = 0;
            StringWriter writer;
            for (const auto &mixTrack : m_mixTracks)
            {
                if (const auto it = m_trackInfosMap.find(mixTrack.trackId); it != m_trackInfosMap.end())
                {
                    writer.appendFormatted("Index {}: orderInMix: {}, MixTrack: ID: {}, Track ID: {}: {} - {}\n",
                        index,
                        mixTrack.orderInMix,
                        it->second->trackId,
                        mixTrack.trackId,
                        it->second->artist_name,
                        it->second->title);
                    assert(index == mixTrack.orderInMix);
                }
                else
                {
                    writer.appendFormatted("MixProjectLoader: Track info not found for track ID: {}\n", mixTrack.trackId);
                }
                ++index;
            }
            spdlog::info("MixProjectLoader: Loaded mix project with ID {}:\n{}", m_mixId, writer.asString());
            spdlog::info("MixProjectLoader: Indexed {} track infos for mix ID {}", m_trackInfosMap.size(), m_mixId);
        }


        bool MixProjectLoader::reorderTracks(const std::vector<std::pair<TrackId, int>>& trackMoves)
        {
            if (trackMoves.empty())
                return true;
                
            // For simplicity, handle single track move first (most common case)
            if (trackMoves.size() == 1)
            {
                const auto& [trackId, newPosition] = trackMoves[0];
                
                // Find the track
                auto it = std::find_if(m_mixTracks.begin(), m_mixTracks.end(),
                                       [trackId](const MixTrack& mt) { return mt.trackId == trackId; });
                if (it == m_mixTracks.end())
                {
                    spdlog::error("Track {} not found in mix", trackId);
                    return false;
                }
                
                // Check for valid position
                if (newPosition < 0 || newPosition >= static_cast<int>(m_mixTracks.size()))
                {
                    spdlog::error("Invalid position {} for track {}", newPosition, trackId);
                    return false;
                }
                
                int currentPosition = std::distance(m_mixTracks.begin(), it);
                if (currentPosition == newPosition)
                {
                    return true; // No change needed
                }
                
                // Move the track
                MixTrack movingTrack = *it;
                m_mixTracks.erase(it);
                m_mixTracks.insert(m_mixTracks.begin() + newPosition, movingTrack);
                
                // Update orderInMix values
                for (int i = 0; i < static_cast<int>(m_mixTracks.size()); ++i)
                {
                    m_mixTracks[i].orderInMix = i;
                }
                
                spdlog::info("Moved track {} from position {} to {}", trackId, currentPosition, newPosition);
                return true;
            }
            
            // For multiple tracks, we'll use a different approach
            // This assumes the tracks are moved as a contiguous block
            std::vector<int> sourcePositions;
            std::vector<MixTrack> tracksToMove;
            
            // Collect source positions and validate
            for (const auto& [trackId, _] : trackMoves)
            {
                auto it = std::find_if(m_mixTracks.begin(), m_mixTracks.end(),
                                       [trackId](const MixTrack& mt) { return mt.trackId == trackId; });
                if (it == m_mixTracks.end())
                {
                    spdlog::error("Track {} not found in mix", trackId);
                    return false;
                }
                
                int position = std::distance(m_mixTracks.begin(), it);
                sourcePositions.push_back(position);
                tracksToMove.push_back(*it);
            }
            
            // Sort source positions to check if they're contiguous
            std::sort(sourcePositions.begin(), sourcePositions.end());
            bool isContiguous = true;
            for (size_t i = 1; i < sourcePositions.size(); ++i)
            {
                if (sourcePositions[i] != sourcePositions[i-1] + 1)
                {
                    isContiguous = false;
                    break;
                }
            }
            
            if (!isContiguous)
            {
                spdlog::warn("Multi-track move with non-contiguous selection not fully supported yet");
                // For now, we'll still handle it but it might not give expected results
            }
            
            // Get target position (use the first track's target)
            int targetPosition = trackMoves[0].second;
            if (targetPosition < 0 || targetPosition > static_cast<int>(m_mixTracks.size()) - static_cast<int>(trackMoves.size()))
            {
                spdlog::error("Invalid target position {} for multi-track move", targetPosition);
                return false;
            }
            
            // Remove tracks from their current positions
            for (auto it = m_mixTracks.begin(); it != m_mixTracks.end(); )
            {
                if (std::any_of(trackMoves.begin(), trackMoves.end(),
                                [&](const auto& move) { return move.first == it->trackId; }))
                {
                    it = m_mixTracks.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            
            // Insert tracks at target position
            m_mixTracks.insert(m_mixTracks.begin() + targetPosition, tracksToMove.begin(), tracksToMove.end());
            
            // Update orderInMix values
            for (int i = 0; i < static_cast<int>(m_mixTracks.size()); ++i)
            {
                m_mixTracks[i].orderInMix = i;
            }
            
            spdlog::info("Moved {} tracks to position {}", trackMoves.size(), targetPosition);
            return true;
        }
        
        bool MixProjectLoader::saveMix(const IMixManager& mixManager) const
        {
            // Create a copy of mix info and tracks to pass to the manager
            MixInfo mixInfoCopy = m_mixInfo;
            std::vector<MixTrack> mixTracksCopy = m_mixTracks;
            
            // Save to database
            if (mixManager.createOrUpdateMix(mixInfoCopy, mixTracksCopy))
            {
                spdlog::info("Successfully saved mix {} with {} tracks", m_mixId, m_mixTracks.size());
                return true;
            }
            else
            {
                spdlog::error("Failed to save mix {}", m_mixId);
                return false;
            }
        }

    } // namespace audio
} // namespace jucyaudio
