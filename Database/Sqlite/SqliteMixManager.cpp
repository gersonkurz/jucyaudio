#include <Database/Includes/Constants.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteMixManager.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Utils/AssortedUtils.h>
#include <Utils/StringWriter.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace
{
    using namespace jucyaudio;
    using namespace jucyaudio::database;

    // to_json
    void envelopePointToJson(json &j, const EnvelopePoint &ep)
    {
        j = json{{"time_ms", ep.time.count()}, // store as integer (milliseconds)
            {"volume", ep.volume}};
    }

    // from_json
    void envelopePointFromJson(const json &j, EnvelopePoint &ep)
    {
        ep.time = Duration_t{j.at("time_ms").get<int64_t>()};
        ep.volume = j.at("volume").get<Volume_t>();
    }

    std::string envelopePointsToJson(const std::vector<EnvelopePoint> &points)
    {
        json j;
        for (const auto &point : points)
        {
            json pointJson;
            envelopePointToJson(pointJson, point);
            j.push_back(pointJson);
        }
        return j.dump(); // Convert to JSON string
    }

    std::vector<EnvelopePoint> envelopePointsFromJson(const std::string &jsonString)
    {
        std::vector<EnvelopePoint> points;
        if (jsonString.empty())
            return points; // Return empty vector if input is empty
        json j = json::parse(jsonString);
        for (const auto &pointJson : j)
        {
            EnvelopePoint point;
            envelopePointFromJson(pointJson, point);
            points.push_back(point);
        }
        return points;
    }

    MixInfo mixInfoFromStatement(const SqliteStatement &stmt)
    {
        MixInfo info{};
        int col = 0;
        info.mixId = stmt.getInt64(col++);
        info.name = stmt.getText(col++);
        info.timestamp = timestampFromInt64(stmt.getInt64(col++));
        info.numberOfTracks = stmt.getInt64(col++);
        info.totalDuration = durationFromInt64(stmt.getInt64(col++));
        if (!stmt.isNull(col))
            info.source_ws_id = stmt.getInt64(col);
        col++;
        if (!stmt.isNull(col))
            info.status = stmt.getText(col);
        col++;
        return info;
    }

    MixTrack mixTrackFromStatement(const SqliteStatement &stmt)
    {
        using namespace jucyaudio;

        MixTrack info{};
        int col = 0;
        info.mixId = stmt.getInt64(col++);
        info.trackId = stmt.getInt64(col++);
        info.orderInMix = stmt.getInt32(col++);
        
        // Deserialize the JSON data
        const std::string jsonData = stmt.getText(col++);
        if (!jsonData.empty())
        {
            json j = json::parse(jsonData);
            // This uses the from_json deserializer from MixInfo.h
            // It only updates the fields stored in JSON (cue, attach, envelope)
            j.get_to(info);
        }
        
        return info;
    }

    bool bindMixTrackToStatement(SqliteStatement &stmt, const MixTrack &info)
    {
        bool ok = true;
        ok &= stmt.addParam(info.mixId);
        ok &= stmt.addParam(info.trackId);
        ok &= stmt.addParam(info.orderInMix);
        
        // Serialize the entire MixTrack data to JSON
        json mixDataJson = info; // This uses the to_json serializer from MixInfo.h
        ok &= stmt.addParam(mixDataJson.dump());

        if (!ok)
        {
            spdlog::error("Failed to bind one or more parameters for MixInfo: {}, {}", info.mixId, info.trackId);
        }
        return ok;
    }
} // namespace

namespace jucyaudio
{
    namespace database
    {
        std::vector<MixInfo> SqliteMixManager::getMixes(const TrackQueryArgs &args) const
        {
            const std::string BASE_STMT = R"SQL(SELECT 
    m.mix_id,
    m.name,
    m.timestamp as created,
    m.track_count,
    m.total_length,
    m.source_ws_id,
    m.status
FROM Mixes m
)SQL";

            StringWriter output;
            output.append(BASE_STMT);
            bool first = true;
            if (!args.searchTerms.empty())
            {
                output.append(" WHERE ");
                for (const auto &searchTerm : args.searchTerms)
                {
                    if (first)
                    {
                        first = false;
                    }
                    else
                    {
                        output.append(" AND ");
                    }
                    output.append("m.name LIKE '%'");
                    output.append(searchTerm);
                    output.append("%'");
                }
            }
            if (args.mixId)
            {
                if (first)
                {
                    output.append(" WHERE ");
                    first = false;
                }
                else
                {
                    output.append(" AND ");
                }
                output.append("m.mix_id = ");
                output.append(std::to_string(args.mixId));
            }
            output.append("\nGROUP BY m.mix_id, m.name, m.timestamp\n");
            if (!args.sortBy.empty())
            {
                output.append("ORDER BY ");
                bool first = true;
                for (const auto &sortOrder : args.sortBy)
                {
                    if (first)
                    {
                        first = false;
                    }
                    else
                    {
                        output.append(", ");
                    }
                    output.append(sortOrder.columnName);
                    if (sortOrder.descending)
                        output.append(" DESC");
                    else
                        output.append(" ASC");
                }
            }
            const auto sql_statement = output.asString();
            spdlog::debug("Executing SQL statement to get mixes: {}", sql_statement);
            std::vector<MixInfo> mixes;
            SqliteStatement stmt{m_db};
            stmt.query(
                [&mixes, &stmt]() -> bool
                {
                    mixes.emplace_back(mixInfoFromStatement(stmt));
                    return true;
                },
                sql_statement);
            return mixes;
        }

        std::vector<MixTrack> SqliteMixManager::getMixTracks(MixId mixId) const
        {
            std::vector<MixTrack> mixTracks;
            SqliteStatement stmt{m_db};
            stmt.query(
                [&mixTracks, &stmt]() -> bool
                {
                    mixTracks.emplace_back(mixTrackFromStatement(stmt));
                    return true;
                },
                "SELECT * FROM MixTracks WHERE mix_id=? ORDER BY order_in_mix ASC",
                mixId);
            return mixTracks;
        }

        bool SqliteMixManager::removeTracksFromMix(MixId mixId, const std::vector<TrackId> &trackIds) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                // Use a prepared statement to remove each track from the mix
                SqliteStatement stmt{m_db, "DELETE FROM MixTracks WHERE mix_id = ? AND track_id = ?"};
                for (const auto &trackId : trackIds)
                {
                    stmt.addParam(mixId);
                    stmt.addParam(trackId);
                    if (!stmt.execute())
                    {
                        return transaction.rollback();
                    }
                    stmt.reset();
                }
                return transaction.commit();
            }
            return false;
        }

        bool SqliteMixManager::removeTrackFromMix(MixId mixId, TrackId trackId) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                SqliteStatement stmt{m_db, "DELETE FROM MixTracks WHERE mix_id = ? AND track_id = ?"};
                stmt.addParam(mixId);
                stmt.addParam(trackId);
                if (stmt.execute())
                {
                    return transaction.commit();
                }
            }
            return false;
        }

        bool SqliteMixManager::createOrUpdateMix(MixInfo &mixInfo, std::vector<MixTrack> &tracks) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                mixInfo.timestamp = std::chrono::system_clock::now();
                mixInfo.numberOfTracks = static_cast<int64_t>(tracks.size());
                assert(mixInfo.totalDuration.count() != 0 || tracks.empty() && "Mix total duration must be set by caller");

                // if mixId is not 0, the mix already exists, so we update it - by first removing all existing data
                if (mixInfo.mixId)
                {
                    if (!transaction.execute("DELETE FROM MixTracks WHERE mix_id = ?;", mixInfo.mixId) ||
                        !transaction.execute("UPDATE Mixes SET name=?, timestamp=?, track_count=?, total_length=? WHERE mix_id=?",
                            mixInfo.name,
                            timestampToInt64(mixInfo.timestamp),
                            mixInfo.numberOfTracks,
                            durationToInt64(mixInfo.totalDuration),
                            mixInfo.mixId))
                    {
                        return transaction.rollback();
                    }
                }
                else
                {
                    if (!transaction.execute("INSERT INTO Mixes (name, timestamp, track_count, total_length, source_ws_id) VALUES (?, ?, ?, ?, ?)",
                            mixInfo.name,
                            timestampToInt64(mixInfo.timestamp),
                            mixInfo.numberOfTracks,
                            durationToInt64(mixInfo.totalDuration),
                            mixInfo.source_ws_id))
                    {
                        return transaction.rollback();
                    }
                    mixInfo.mixId = m_db.getLastInsertRowId(); // Get the new mix ID
                }
                assert(mixInfo.mixId != 0 && "Mix ID should be set after insert/update");

                for (auto &track : tracks)
                {
                    track.mixId = mixInfo.mixId;
                    SqliteStatement stmt_insert{m_db,
                        "INSERT INTO MixTracks (mix_id,track_id,order_in_mix,mix_data) "
                        "VALUES (?,?,?,?)"};
                    bindMixTrackToStatement(stmt_insert, track);
                    if (!stmt_insert.execute())
                    {
                        return transaction.rollback();
                    }
                }
                return transaction.commit();
            }
            return false;
        }

        bool SqliteMixManager::removeMix(MixId mixId) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                if (transaction.execute("DELETE FROM MixTracks WHERE mix_id = ?;", mixId))
                {
                    if (transaction.execute("DELETE FROM Mixes WHERE mix_id = ?;", mixId))
                    {
                        return transaction.commit();
                    }
                }
            }
            return false;
        }

        bool SqliteMixManager::removeMixes(const std::vector<MixId> &mixIds) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                for (const auto mixId : mixIds)
                {
                    if (!transaction.execute("DELETE FROM MixTracks WHERE mix_id = ?;", mixId))
                    {
                        return transaction.rollback();
                    }
                    if (!transaction.execute("DELETE FROM Mixes WHERE mix_id = ?;", mixId))
                    {
                        return transaction.rollback();
                    }
                }
                return transaction.commit();
            }
            return false;
        }

        bool SqliteMixManager::finalizeMix(MixId mixId) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                // 1. Fetch mix status and source working set ID
                SqliteStatement stmt_info{m_db, "SELECT status, source_ws_id FROM Mixes WHERE mix_id = ?;"};
                stmt_info.addParam(mixId);
                if (!stmt_info.getNextResult())
                {
                    spdlog::error("FinalizeMix: Could not find mix with ID {}", mixId);
                    return transaction.rollback();
                }

                std::string status = stmt_info.getText(0);
                std::optional<WorkingSetId> source_ws_id;
                if (!stmt_info.isNull(1))
                {
                    source_ws_id = stmt_info.getInt64(1);
                }

                // 2. Check if the mix is new and has a source
                if (status == "New" && source_ws_id.has_value())
                {
                    // 3a. Identify all tracks in the mix up to the last active track
                    SqliteStatement stmt_prune{m_db,
                        "SELECT track_id FROM MixTracks WHERE mix_id = ? AND order_in_mix <= "
                        "(SELECT MAX(order_in_mix) FROM MixTracks WHERE mix_id = ?)"};
                    stmt_prune.addParam(mixId);
                    stmt_prune.addParam(mixId);

                    std::vector<TrackId> tracksToPrune;
                    while (stmt_prune.getNextResult())
                    {
                        tracksToPrune.push_back(stmt_prune.getInt64(0));
                    }

                    // 3b. Prune these tracks from the source Working Set
                    if (!tracksToPrune.empty())
                    {
                        StringWriter delete_query;
                        delete_query.append("DELETE FROM WorkingSetTracks WHERE ws_id = ? AND track_id IN (");
                        for (size_t i = 0; i < tracksToPrune.size(); ++i)
                        {
                            delete_query.append(std::to_string(tracksToPrune[i]));
                            if (i < tracksToPrune.size() - 1)
                                delete_query.append(",");
                        }
                        delete_query.append(");");

                        SqliteStatement stmt_delete{m_db, delete_query.asString()};
                        stmt_delete.addParam(source_ws_id.value());
                        if (!stmt_delete.execute())
                        {
                            spdlog::error("FinalizeMix: Failed to prune tracks from working set {}", source_ws_id.value());
                            return transaction.rollback();
                        }
                    }

                    // 3c. Update the mix's status to 'Finalized'
                    if (!transaction.execute("UPDATE Mixes SET status = 'Finalized' WHERE mix_id = ?;", mixId))
                    {
                        spdlog::error("FinalizeMix: Failed to update mix status for mix ID {}", mixId);
                        return transaction.rollback();
                    }
                }

                return transaction.commit();
            }
            return false;
        }

        bool SqliteMixManager::createAndSaveAutoMix(const std::vector<TrackInfo> &trackInfos,
            /*in/out*/ MixInfo &mixInfo,
            /*out*/ std::vector<MixTrack> &resultingTracks,
            WorkingSetId source_ws_id,
            const Duration_t defaultCrossfadeDuration) const
        {
            // New ATTACH-based automix implementation
            assert(resultingTracks.empty() && "resultingTracks should be empty before creating a new mix");
            assert(!trackInfos.empty() && "trackInfos should not be empty when creating a new mix");

            mixInfo.numberOfTracks = static_cast<int64_t>(trackInfos.size());
            mixInfo.source_ws_id = source_ws_id;

            const auto minimumExpectedSongLength = 2 * defaultCrossfadeDuration; // Minimum length for a track to be suitable for mixing
            spdlog::info("Creating ATTACH-based automix with {} tracks, crossfade duration: {}", 
                        trackInfos.size(), durationToString(defaultCrossfadeDuration));

            int orderInMix = 0;
            for (const auto &trackInfo : trackInfos)
            {
                assert(trackInfo.trackId != 0 && "Track ID should not be zero when creating a new mix");

                // check track is longer than minimumExpectedSongLength - otherwise it's not suitable for mixing
                if (trackInfo.duration < minimumExpectedSongLength)
                {
                    spdlog::debug("Track {} ({}) is only {} long: too short for mixing, skipping",
                        trackInfo.trackId,
                        pathToString(trackInfo.filepath),
                        durationToString(trackInfo.duration));
                    continue;
                }

                MixTrack mixTrack{};
                mixTrack.mixId = mixInfo.mixId;
                mixTrack.trackId = trackInfo.trackId;
                mixTrack.orderInMix = orderInMix++;

                // For the new model:
                // CUE points - use full track (default behavior)
                mixTrack.cueStart = Duration_t{0};
                mixTrack.cueEnd = Duration_t{0}; // 0 means use full track duration
                
                // ATTACH points - define where overlaps happen
                // For a simple automix, let's use symmetrical attach points
                mixTrack.attachFrom = defaultCrossfadeDuration;  // Start overlapping after 5s into this track
                mixTrack.attachTo = trackInfo.duration - defaultCrossfadeDuration; // Next track starts 5s before this ends
                
                // Create envelope points for crossfade
                // These are relative to the track's cue start (which is 0 in this case)
                const auto fadeInMidpoint = Duration_t{2000};  // 2 seconds
                const auto fadeOutMidpoint = trackInfo.duration - Duration_t{2000}; // 2 seconds before end

                mixTrack.envelopePoints = {
                    {Duration_t{0}, Volume_t{200}},                                        // Start at 20%
                    {fadeInMidpoint, Volume_t{700}},                                       // 2s: 70%
                    {defaultCrossfadeDuration, VOLUME_NORMALIZATION},                      // 5s: 100%
                    {trackInfo.duration - defaultCrossfadeDuration, VOLUME_NORMALIZATION}, // duration-5s: 100%
                    {fadeOutMidpoint, Volume_t{700}},                                      // duration-2s: 70%
                    {trackInfo.duration, Volume_t{200}}                                    // End at 20%
                };

                resultingTracks.emplace_back(mixTrack);
            }
            
            // Calculate total duration by walking the chain
            // This is now computed from the ATTACH points
            if (!resultingTracks.empty())
            {
                Duration_t mixEndPosition{0};
                Duration_t previousTrackStart{0};
                
                for (size_t i = 0; i < resultingTracks.size(); ++i)
                {
                    const auto& track = resultingTracks[i];
                    
                    // Find the matching trackInfo by track ID
                    const auto trackInfoIt = std::find_if(trackInfos.begin(), trackInfos.end(),
                        [&track](const auto& ti) { return ti.trackId == track.trackId; });
                    
                    if (trackInfoIt == trackInfos.end())
                    {
                        spdlog::error("Could not find track info for track ID {} while calculating duration", track.trackId);
                        continue;
                    }
                    
                    const auto& trackInfo = *trackInfoIt;
                    Duration_t trackStart{0};
                    
                    if (i == 0)
                    {
                        // First track starts at position 0
                        trackStart = Duration_t{0};
                    }
                    else
                    {
                        // ATTACH formula: Next track start = Previous track start + Previous track's attachTo - Current track's attachFrom
                        const auto& prevTrack = resultingTracks[i-1];
                        trackStart = previousTrackStart + prevTrack.attachTo - track.attachFrom;
                    }
                    
                    // Track end position
                    Duration_t trackEnd = trackStart + trackInfo.duration;
                    
                    // Update mix end to be the latest track end
                    if (trackEnd > mixEndPosition)
                    {
                        mixEndPosition = trackEnd;
                    }
                    
                    // Remember this track's start for the next iteration
                    previousTrackStart = trackStart;
                }
                
                mixInfo.totalDuration = mixEndPosition;
            }
            else
            {
                mixInfo.totalDuration = Duration_t{0};
            }
            
            spdlog::info("Automix created: {} tracks, total duration: {}", 
                        resultingTracks.size(), durationToString(mixInfo.totalDuration));

            // Store in database
            return createOrUpdateMix(mixInfo, resultingTracks);
        }
        bool SqliteMixManager::renameMix(MixId mixId, std::string_view name) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                if (transaction.execute("UPDATE Mixes SET name=? WHERE mix_id=?;", name, mixId))
                {
                    return transaction.commit();
                }
                return transaction.commit();
            }
            return false;
        }


    } // namespace database
} // namespace jucyaudio
