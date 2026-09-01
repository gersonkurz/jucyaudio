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

            // Cleared first, so that every early return below leaves this loader marked unloaded
            // rather than carrying the previous mix's answer.
            m_loaded = false;
            m_loadAttempted = true;

            // Everything is read into locals and only published at the end.
            //
            // Clearing them here and then failing would leave this loader describing half a mix: the
            // previous one gone, the new one never read. A failed load leaves the previous mix intact
            // and merely marked unloaded, so callers still see something coherent while m_loaded stops
            // anything writing the stale contents back.
            const auto mixInfo = theTrackLibrary.getMixManager().getMix(mixId);
            if (mixInfo.mixId == 0)
            {
                spdlog::error("MixProjectLoader: No mix found with ID {}", mixId);
                return false; // No mix found
            }

            // readMixTracks, not getMixTracks: this result is saved back, and saveMix rewrites the
            // whole row set. Accepting a partly-read mix here would delete every track after the one
            // that could not be read, the first time the user saved.
            std::vector<MixTrack> loadedTracks;
            if (const auto read = theTrackLibrary.getMixManager().readMixTracks(mixId, loadedTracks); !read.isOk())
            {
                spdlog::error("MixProjectLoader: Could not load the tracks of mix {}: {}", mixId, read.errorMessage);
                return false;
            }

            // Status-bearing, because the answer decides whether this mix may be edited. The statusless
            // form reports a failed query and an empty result the same way, and reports a read that
            // stopped partway as a shorter answer.
            //
            // What that costs is not a saved row set full of holes - the rows come from readMixTracks
            // above, which is checked, and saveMix writes those. It is that the editor draws what this
            // read returned. A failed read makes the mix look shorter than it is, or empty, and every
            // edit the user then makes - reordering, deleting, pasting - is made against a picture of
            // a mix that is missing tracks which really are there. Those edits are saved from the rows,
            // and the rows are real, so the mix is quietly rewritten to match a view that was wrong.
            //
            // What is *not* treated as failure is a short answer that the query meant: this query
            // filters out offline folders, so a perfectly good mix whose tracks are all on a
            // disconnected drive resolves to nothing at all. Refusing that would make those mixes
            // unopenable whenever the volume is unplugged, which is a worse failure than the one being
            // guarded against. Such a track still holds its place - every positioning walk advances
            // through every row - and contributes nothing to the mix's length; the timeline draws no
            // component for it, and a WAV or MP3 export refuses outright.
            //
            // So: the query failing is fatal, the query answering with less than the whole mix is not.
            std::vector<TrackInfo> loadedInfos;
            if (const auto read = theTrackLibrary.getTracks(getMixTrackQueryArgs(mixId), loadedInfos); !read.isOk())
            {
                spdlog::error("MixProjectLoader: Could not read the tracks of mix {}: {}", mixId, read.errorMessage);
                return false;
            }

            spdlog::info("[RELOAD] MixProjectLoader::loadMix - {} tracks and {} track infos for mix ID {}",
                loadedTracks.size(),
                loadedInfos.size(),
                mixId);

            // Published together, after nothing else can fail. The id included: a failed load
            // must leave this loader describing the mix it described before, not the new id
            // with the old mix's tracks still attached to it.
            m_mixId = mixId;
            m_mixInfo = mixInfo;
            m_mixTracks = std::move(loadedTracks);
            m_trackInfos = std::move(loadedInfos);

            rebuildTrackInfoMap();

            // With the new contents, not before them: anything comparing against this number is asking
            // whether what it cached still describes what is here now.
            ++m_contentsGeneration;
            //int index = 0;
            
            // Only dump context in debug builds or when explicitly debugging
            #ifdef DEBUG_MIX_LOADING
            dumpContext(__FILE__, __LINE__);
            #endif
            spdlog::debug("MixProjectLoader: Indexed {} track infos for mix ID {}", m_trackInfosMap.size(), m_mixId);

            // Last, so that only a run reaching this point counts as loaded.
            m_loaded = true;
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

            // A row is gone and the rest have been renumbered, so anything holding a copy of the old
            // set is describing positions that have moved.
            ++m_contentsGeneration;

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

            // The rows have moved, so anything holding a copy of the old order is naming positions that
            // now belong to other tracks. Bumped here rather than only in loadMix because this happens
            // without one: the caller reorders, then saves, and a save that fails never reloads.
            ++m_contentsGeneration;

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
            // The invariant belongs here, not in each caller. createOrUpdateMix replaces the whole row
            // set, so writing a track list that did not come out of the database rewrites the mix to
            // match whatever this object happens to be holding - and after a failed load that is either
            // nothing or, worse, the previous mix's tracks left in place when getMix failed.
            //
            // A loader that has never tried to read is one building a new mix, and has nothing to
            // have loaded. A loader that tried and failed is a different thing entirely, even though
            // both can be sitting on a mix id of zero - a first load of an existing mix that fails
            // leaves the id unpublished, so the id alone cannot tell them apart.
            if (m_loadAttempted && !m_loaded)
            {
                spdlog::error("[SAVE_MIX] Refusing to save mix {}: its tracks were never loaded.", m_mixId);
                return false;
            }

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
                // What is held in memory is now exactly what the database holds, so it is safe to write
                // again. Without this a newly created mix could be saved once and never again: the save
                // gives it an id, and the next save would see a real id with nothing ever loaded.
                m_loaded = true;

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
