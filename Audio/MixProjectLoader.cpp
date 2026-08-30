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

        void MixProjectLoader::rebuildTrackInfoMap()
        {
            m_trackInfosMap.clear();
            for (const auto &ti : m_trackInfos)
            {
                m_trackInfosMap[ti.trackId] = &ti;
            }
        }

        bool MixProjectLoader::loadMix(MixId mixId)
        {
            spdlog::debug("MixProjectLoader: Loading mix with ID {}", mixId);
            m_mixId = mixId;
            m_mixInfo = theTrackLibrary.getMixManager().getMix(m_mixId);
            if (m_mixInfo.mixId == 0)
            {
                spdlog::error("MixProjectLoader: No mix found with ID {}", m_mixId);
                return false; // No mix found
            }
            m_mixTracks = theTrackLibrary.getMixManager().getMixTracks(m_mixId);
            spdlog::info("[RELOAD] MixProjectLoader::loadMix - Loaded {} tracks for mix ID {} from database", m_mixTracks.size(), m_mixId);
            
            // Log first few tracks for debugging
            if (!m_mixTracks.empty())
            {
                spdlog::info("[RELOAD] First few tracks loaded:");
                for (size_t i = 0; i < std::min(size_t(3), m_mixTracks.size()); ++i)
                {
                    const auto& track = m_mixTracks[i];
                    spdlog::info("[RELOAD]   - Track {} at position {}", track.trackId, track.orderInMix);
                }
            }
            m_trackInfos = theTrackLibrary.getTracks(getMixTrackQueryArgs(m_mixId));
            spdlog::info("MixProjectLoader: Loaded {} track infos for mix ID {}", m_trackInfos.size(), m_mixId);

            rebuildTrackInfoMap();
            //int index = 0;
            
            // Only dump context in debug builds or when explicitly debugging
            #ifdef DEBUG_MIX_LOADING
            dumpContext(__FILE__, __LINE__);
            #endif
            spdlog::debug("MixProjectLoader: Indexed {} track infos for mix ID {}", m_trackInfosMap.size(), m_mixId);
            return true;
        }

        bool MixProjectLoader::reloadFromDatabase()
        {
            return loadMix(m_mixId);
        }

        bool MixProjectLoader::removeTrackAtOrder(int orderInMix)
        {
            if (orderInMix < 0 || orderInMix >= static_cast<int>(m_mixTracks.size()))
            {
                spdlog::error("MixProjectLoader::removeTrackAtOrder - Invalid order {}", orderInMix);
                return false;
            }

            const auto removedTrackId = m_mixTracks[orderInMix].trackId;
            m_mixTracks.erase(m_mixTracks.begin() + orderInMix);

            for (int i = orderInMix; i < static_cast<int>(m_mixTracks.size()); ++i)
            {
                m_mixTracks[i].orderInMix = i;
            }

            const bool stillReferenced = std::any_of(
                m_mixTracks.begin(),
                m_mixTracks.end(),
                [removedTrackId](const MixTrack &track)
                {
                    return track.trackId == removedTrackId;
                });

            if (!stillReferenced)
            {
                m_trackInfos.erase(
                    std::remove_if(
                        m_trackInfos.begin(),
                        m_trackInfos.end(),
                        [removedTrackId](const TrackInfo &trackInfo)
                        {
                            return trackInfo.trackId == removedTrackId;
                        }),
                    m_trackInfos.end());
            }

            rebuildTrackInfoMap();
            return true;
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
            mixTable.add_row({"#", "Track ID", "Artist - Title", "Duration", "Cue Start", "Cue End", 
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
                    envelopeStr += durationToString(mixTrack.envelopePoints[i].time) + ":" +
                                  std::to_string(mixTrack.envelopePoints[i].volume);
                }
                if (envelopeStr.empty()) envelopeStr = "[none]";
                
                mixTable.add_row({
                    std::to_string(mixTrack.orderInMix),
                    std::to_string(mixTrack.trackId),
                    artistTitle,
                    duration,
                    mixTrack.cueStart.count() == 0 ? "[start]" : durationToString(mixTrack.cueStart),
                    mixTrack.cueEnd.count() == 0 ? "[end]" : 
                    mixTrack.cueEnd.count() < 0 ? "[end" + std::to_string(mixTrack.cueEnd.count()/1000) + "s]" :
                    durationToString(mixTrack.cueEnd),
                    durationToString(mixTrack.attachFrom),
                    durationToString(mixTrack.attachTo),
                    envelopeStr
                });
            }
            
            // Style the table
            mixTable.format()
                .border_top("-")
                .border_bottom("-")
                .border_left("|")
                .border_right("|")
                .corner("+");
            
            // Make the output both human and machine readable
            std::ostringstream oss;
            oss << mixTable;
            spdlog::info("MixProjectLoader: Dumping mix context for mix ID {}:\n{}", m_mixId, oss.str());
            
            calculateMixDuration();
        }

        Duration_t MixProjectLoader::calculateMixDuration() const
        {
            // The walk itself lives in MixInfo.h, so that the mix manager computes the same number
            // when it writes total_length. It used to live only here, which meant every save path
            // that did not go through this class stored whatever total it happened to be carrying.
            const auto duration = database::calculateMixDuration(m_mixTracks,
                [this](TrackId trackId) -> std::optional<Duration_t>
                {
                    const auto it = m_trackInfosMap.find(trackId);
                    if (it == m_trackInfosMap.end())
                    {
                        return std::nullopt;
                    }
                    return it->second->duration;
                });

            spdlog::info("Total mix duration: {} ({} tracks)", durationToString(duration), m_mixTracks.size());
            return duration;
        }

        bool MixProjectLoader::reorderSingleTrack(TrackId trackId, int newPosition)
        {
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

            spdlog::info("Moving track {} from position {} to {}", trackId, currentPosition, newPosition);

            // In the ATTACH model, we only need to reorder the tracks and update orderInMix
            // The timeline positions are calculated dynamically based on attach points
            MixTrack movingTrack = *it;
            
            // Remove and reinsert the track at its new position
            m_mixTracks.erase(it);
            m_mixTracks.insert(m_mixTracks.begin() + newPosition, movingTrack);

            // Update orderInMix values
            for (int i = 0; i < static_cast<int>(m_mixTracks.size()); ++i)
            {
                m_mixTracks[i].orderInMix = i;
            }

            spdlog::info("Successfully moved track {} from position {} to {}", 
                trackId, currentPosition, newPosition);
            return true;
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
                
                //int currentPos = std::distance(m_mixTracks.begin(), it);
                
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

        bool MixProjectLoader::saveMix(const IMixManager &mixManager)
        {
            spdlog::info("[SAVE_MIX] MixProjectLoader::saveMix() called, m_mixTracks.size() = {}", m_mixTracks.size());
            
            // Log first few tracks in m_mixTracks for debugging
            if (!m_mixTracks.empty())
            {
                spdlog::info("[SAVE_MIX] First few tracks in m_mixTracks:");
                for (size_t i = 0; i < std::min(size_t(5), m_mixTracks.size()); ++i)
                {
                    const auto& track = m_mixTracks[i];
                    spdlog::info("[SAVE_MIX]   - Track {} at position {}", track.trackId, track.orderInMix);
                }
                if (m_mixTracks.size() > 5)
                {
                    spdlog::info("[SAVE_MIX]   ... and {} more tracks", m_mixTracks.size() - 5);
                }
            }
            
            // Create a copy of mix info and tracks to pass to the manager
            std::vector<MixTrack> mixTracksCopy = m_mixTracks;
            m_mixInfo.totalDuration = calculateMixDuration();
            
            spdlog::info("[SAVE_MIX] Passing {} tracks to createOrUpdateMix", mixTracksCopy.size());

            // Save to database - actually, might also create it
            if (mixManager.createOrUpdateMix(m_mixInfo, mixTracksCopy))
            {
                if (m_mixId == 0)
                {
                    m_mixId = m_mixInfo.mixId;
                    spdlog::info("Created new mix with ID {} for {} tracks", m_mixId, m_mixTracks.size());
                }
                else
                {
                    spdlog::info("Updated existing mix with ID {} for {} tracks", m_mixId, m_mixTracks.size());
                }
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
