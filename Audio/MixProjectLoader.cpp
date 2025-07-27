#include <Audio/MixProjectLoader.h>
#include <Utils/AssortedUtils.h>
#include <Utils/StringWriter.h>
#include <algorithm>
#include <unordered_set>
#include <tabulate/table.hpp>
#include <sstream>

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
            
            dumpContext(__FILE__, __LINE__);
            spdlog::info("MixProjectLoader: Indexed {} track infos for mix ID {}", m_trackInfosMap.size(), m_mixId);
        }

        void MixProjectLoader::dumpContext(const char* file, int line) const
        {
            using namespace tabulate;
            
            spdlog::info("MixProjectLoader: Context dump at {}:{} for mix ID {}", file, line, m_mixId);
            
            if (m_mixTracks.empty())
            {
                spdlog::info("  [Mix is empty]");
                return;
            }
            
            Table mixTable;
            mixTable.add_row({"#", "Track ID", "Artist - Title", "Cue Start", "Cue End", 
                             "Attach From", "Attach To", "Envelope Points"});
            
            // Style the header row
            mixTable[0].format()
                .font_color(Color::cyan)
                .font_style({FontStyle::bold});
            
            for (const auto &mixTrack : m_mixTracks)
            {
                std::string artistTitle = "???";
                std::string duration = "";
                
                if (const auto it = m_trackInfosMap.find(mixTrack.trackId); it != m_trackInfosMap.end())
                {
                    artistTitle = it->second->artist_name + " - " + it->second->title;
                    duration = durationToString(it->second->duration);
                }
                
                // Format envelope points as compact string
                std::string envelopeStr;
                for (size_t i = 0; i < mixTrack.envelopePoints.size(); ++i)
                {
                    if (i > 0) envelopeStr += ", ";
                    envelopeStr += std::to_string(mixTrack.envelopePoints[i].time.count()) + "ms:" +
                                  std::to_string(mixTrack.envelopePoints[i].volume);
                }
                if (envelopeStr.empty()) envelopeStr = "[none]";
                
                mixTable.add_row({
                    std::to_string(mixTrack.orderInMix),
                    std::to_string(mixTrack.trackId),
                    artistTitle,
                    durationToString(mixTrack.cueStart),
                    mixTrack.cueEnd.count() < 0 ? "[end]" : durationToString(mixTrack.cueEnd),
                    durationToString(mixTrack.attachFrom),
                    durationToString(mixTrack.attachTo),
                    envelopeStr
                });
            }
            
            // Style the table
            mixTable.format()
                .border_top("─")
                .border_bottom("─")
                .border_left("│")
                .border_right("│")
                .corner("┼");
            
            // Make the output both human and machine readable
            std::ostringstream oss;
            oss << mixTable;
            spdlog::info("MixProjectLoader: Dumping mix context for mix ID {}:\n{}", m_mixId, oss.str());
        }

        bool MixProjectLoader::reorderSingleTrack(TrackId trackId, int newPosition)
        {
#if MIX_TRANSITION_TRACK_REORDERING_AVAILABLE
            // Find the track
            auto it = std::find_if(m_mixTracks.begin(),
                m_mixTracks.end(),
                [trackId](const MixTrack &mt)
                {
                    return mt.trackId == trackId;
                });
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

            // Store the moving track's time information
            MixTrack movingTrack = *it;
            const auto holeDuration = movingTrack.mixEndTime - movingTrack.mixStartTime;

            // Determine which tracks need time adjustment
            const int minPos = std::min(currentPosition, newPosition);
            const int maxPos = std::max(currentPosition, newPosition);
            const bool movingForward = newPosition < currentPosition;

            spdlog::info(
                "Moving track {} from position {} to {}, hole duration: {}ms", trackId, currentPosition, newPosition, durationToString(holeDuration));

            // Calculate new start time for the moved track
            Duration_t newStartTime{};
            if (newPosition == 0)
            {
                // Moving to first position - start at 0
                newStartTime = static_cast<Duration_t>(0);
            }
            else if (movingForward)
            {
                // Moving forward - position after track at (newPosition - 1)
                newStartTime = m_mixTracks[newPosition - 1].mixEndTime;
            }
            else
            {
                // Moving backward - position after what will be at (newPosition - 1) after the move
                // Since we're moving backward, the track currently at newPosition will be at newPosition - 1
                newStartTime = m_mixTracks[newPosition].mixEndTime;
            }

            // Adjust times for affected tracks
            for (int i = minPos; i <= maxPos; ++i)
            {
                if (i == currentPosition)
                    continue; // Skip the moving track

                if (movingForward && i >= newPosition && i < currentPosition)
                {
                    // Tracks that need to shift right
                    m_mixTracks[i].mixStartTime += holeDuration;
                    m_mixTracks[i].mixEndTime += holeDuration;
                    spdlog::debug("Shifted track at position {} right by {}ms", i, holeDuration);
                }
                else if (!movingForward && i > currentPosition && i <= newPosition)
                {
                    // Tracks that need to shift left
                    m_mixTracks[i].mixStartTime -= holeDuration;
                    m_mixTracks[i].mixEndTime -= holeDuration;
                    spdlog::debug("Shifted track at position {} left by {}ms", i, holeDuration);
                }
            }

            // Update the moving track's time
            movingTrack.mixStartTime = newStartTime;
            movingTrack.mixEndTime = newStartTime + holeDuration;

            // Remove and reinsert the track at its new position
            m_mixTracks.erase(it);
            m_mixTracks.insert(m_mixTracks.begin() + newPosition, movingTrack);

            // Update orderInMix values
            for (int i = 0; i < static_cast<int>(m_mixTracks.size()); ++i)
            {
                m_mixTracks[i].orderInMix = i;
            }

            spdlog::info("Moved track {} from position {} to {}, new time range: [{}, {}]ms",
                trackId,
                currentPosition,
                newPosition,
                movingTrack.mixStartTime,
                movingTrack.mixEndTime);
            return true;
#else
            return false;
#endif // MIX_TRANSITION_TRACK_REORDERING_AVAILABLE
        }

        bool MixProjectLoader::reorderTracks(const std::vector<std::pair<TrackId, int>> &trackMoves)
        {
            
            if (trackMoves.empty())
                return true;

            // For single track, use the optimized single-track function
            if (trackMoves.size() == 1)
            {
                const auto &[trackId, newPosition] = trackMoves[0];
                return reorderSingleTrack(trackId, newPosition);
            }

            // For multiple tracks, apply moves one by one
            // Sort moves by their target position to avoid conflicts
            auto sortedMoves = trackMoves;
            std::sort(sortedMoves.begin(), sortedMoves.end(), 
                [](const auto& a, const auto& b) { return a.second < b.second; });

            // Apply each move
            for (const auto& [trackId, targetPosition] : sortedMoves)
            {
                // Find current position of this track (it may have shifted due to previous moves)
                auto it = std::find_if(m_mixTracks.begin(), m_mixTracks.end(),
                    [trackId](const MixTrack& mt) { return mt.trackId == trackId; });
                
                if (it == m_mixTracks.end())
                {
                    spdlog::error("Track {} not found during multi-track reorder", trackId);
                    return false;
                }
                
                int currentPos = std::distance(m_mixTracks.begin(), it);
                
                // Adjust target position based on how many tracks we've already moved
                int adjustedTarget = targetPosition;
                
                // Count how many tracks with lower target positions have already been processed
                // and are currently before this target position
                for (const auto& [prevTrackId, prevTarget] : sortedMoves)
                {
                    if (prevTrackId == trackId) break; // Don't count ourselves
                    
                    auto prevIt = std::find_if(m_mixTracks.begin(), m_mixTracks.end(),
                        [prevTrackId](const MixTrack& mt) { return mt.trackId == prevTrackId; });
                    
                    if (prevIt != m_mixTracks.end())
                    {
                        int prevCurrentPos = std::distance(m_mixTracks.begin(), prevIt);
                        if (prevTarget < targetPosition && prevCurrentPos < adjustedTarget)
                        {
                            // This track is already in place and affects our target
                            adjustedTarget = std::max(adjustedTarget - 1, 0);
                        }
                    }
                }
                
                if (!reorderSingleTrack(trackId, adjustedTarget))
                {
                    spdlog::error("Failed to move track {} to position {}", trackId, adjustedTarget);
                    return false;
                }
            }
            dumpContext(__FILE__, __LINE__);

            spdlog::info("Successfully moved {} tracks", trackMoves.size());
            return true;
        }

        bool MixProjectLoader::saveMix(const IMixManager &mixManager) const
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
