#include <Database/Includes/Constants.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteMixManager.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <UI/Settings.h>
#include <Utils/AssortedUtils.h>
#include <Utils/StringWriter.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <algorithm>

using json = nlohmann::json;

namespace
{
    using namespace jucyaudio;
    using namespace jucyaudio::database;

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

        // Check if we have the export fields (they might not be present in all queries)
        if (stmt.getNumberOfColumns() > col)
        {
            if (!stmt.isNull(col))
                info.exportedAt = timestampFromInt64(stmt.getInt64(col));
            col++;
        }
        if (stmt.getNumberOfColumns() > col)
        {
            if (!stmt.isNull(col))
                info.exportFolder = stmt.getText(col);
            col++;
        }

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
            // Check if we need to filter offline mixes
            const bool filterOffline = !config::theSettings.uiSettings.showOfflineTracks;
            
            // Check if the temp table exists (only created when there are offline folders)
            bool tempTableExists = false;
            if (filterOffline)
            {
                SqliteStatement checkStmt{m_db, "SELECT name FROM sqlite_temp_master WHERE type='table' AND name='OfflineMixes';"};
                tempTableExists = checkStmt.getNextResult();
            }
            
            std::string BASE_STMT;
            if (filterOffline && tempTableExists)
            {
                // Filter out offline mixes and exported mixes
                BASE_STMT = R"SQL(SELECT
    m.mix_id,
    m.name,
    m.timestamp as created,
    m.track_count,
    m.total_length,
    m.source_ws_id,
    m.status,
    m.exported_at,
    m.export_folder
FROM Mixes m
WHERE m.mix_id NOT IN (SELECT mix_id FROM temp.OfflineMixes)
AND m.export_folder IS NULL
)SQL";
            }
            else
            {
                // Show all non-exported mixes
                BASE_STMT = R"SQL(SELECT
    m.mix_id,
    m.name,
    m.timestamp as created,
    m.track_count,
    m.total_length,
    m.source_ws_id,
    m.status,
    m.exported_at,
    m.export_folder
FROM Mixes m
WHERE m.export_folder IS NULL
)SQL";
            }

            StringWriter output;
            output.append(BASE_STMT);
            bool first = true;
            if (!args.searchTerms.empty())
            {
                // We now always have a WHERE clause (for export_folder IS NULL)
                output.append(" AND ");
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
                    output.append("m.name LIKE '%");
                    output.append(searchTerm);
                    output.append("%'");
                }
            }
            if (args.mixId)
            {
                // We now always have a WHERE clause (for export_folder IS NULL)
                output.append(" AND ");
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
                // First, collect the orderInMix values of tracks being deleted
                std::vector<int> deletedOrders;
                for (const auto &trackId : trackIds)
                {
                    SqliteStatement getOrderStmt{m_db, "SELECT order_in_mix FROM MixTracks WHERE mix_id = ? AND track_id = ?"};
                    getOrderStmt.addParam(mixId);
                    getOrderStmt.addParam(trackId);
                    if (getOrderStmt.getNextResult())
                    {
                        deletedOrders.push_back(getOrderStmt.getInt32(0));
                    }
                }
                
                // Sort the orders so we can calculate the shift correctly
                std::sort(deletedOrders.begin(), deletedOrders.end());
                
                // Delete all the tracks
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
                
                // Re-enumerate remaining tracks
                // For each deleted position, shift down all tracks above it
                for (size_t i = 0; i < deletedOrders.size(); ++i)
                {
                    const int adjustedOrder = deletedOrders[i] - static_cast<int>(i); // Account for previous shifts
                    SqliteStatement updateStmt{m_db, "UPDATE MixTracks SET order_in_mix = order_in_mix - 1 WHERE mix_id = ? AND order_in_mix > ?"};
                    updateStmt.addParam(mixId);
                    updateStmt.addParam(adjustedOrder);
                    if (!updateStmt.execute())
                    {
                        spdlog::error("Failed to re-enumerate orderInMix after batch track deletion");
                        return transaction.rollback();
                    }
                }
                
                if (!deletedOrders.empty())
                {
                    spdlog::info("Re-enumerated orderInMix after deleting {} tracks", trackIds.size());
                }
                
                return transaction.commit();
            }
            return false;
        }

        bool SqliteMixManager::removeTrackFromMix(MixId mixId, TrackId trackId) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                // First, get the orderInMix of the track being deleted
                SqliteStatement getOrderStmt{m_db, "SELECT order_in_mix FROM MixTracks WHERE mix_id = ? AND track_id = ?"};
                getOrderStmt.addParam(mixId);
                getOrderStmt.addParam(trackId);
                
                int deletedTrackOrder = -1;
                if (getOrderStmt.getNextResult())
                {
                    deletedTrackOrder = getOrderStmt.getInt32(0);
                }
                
                // Delete the track
                SqliteStatement stmt{m_db, "DELETE FROM MixTracks WHERE mix_id = ? AND track_id = ?"};
                stmt.addParam(mixId);
                stmt.addParam(trackId);
                if (!stmt.execute())
                {
                    return transaction.rollback();
                }
                
                // Re-enumerate orderInMix for all tracks after the deleted one
                if (deletedTrackOrder >= 0)
                {
                    SqliteStatement updateStmt{m_db, "UPDATE MixTracks SET order_in_mix = order_in_mix - 1 WHERE mix_id = ? AND order_in_mix > ?"};
                    updateStmt.addParam(mixId);
                    updateStmt.addParam(deletedTrackOrder);
                    if (!updateStmt.execute())
                    {
                        spdlog::error("Failed to re-enumerate orderInMix after track deletion");
                        return transaction.rollback();
                    }
                    spdlog::info("Re-enumerated orderInMix for tracks after position {}", deletedTrackOrder);
                }
                
                return transaction.commit();
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
                else if (mixInfo.source_ws_id)
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
                else 
                {
                    if (!transaction.execute("INSERT INTO Mixes (name, timestamp, track_count, total_length, source_ws_id) VALUES (?, ?, ?, ?, NULL)",
                            mixInfo.name,
                            timestampToInt64(mixInfo.timestamp),
                            mixInfo.numberOfTracks,
                            durationToInt64(mixInfo.totalDuration)))                            
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
        
        bool SqliteMixManager::clearMixWorkingSetId(MixId mixId) const
        {
            SqliteStatement stmt{m_db, "UPDATE Mixes SET source_ws_id = NULL WHERE mix_id = ?;"};
            stmt.addParam(mixId);
            
            if (!stmt.execute())
            {
                spdlog::error("Failed to clear working_set_id for mix {}", mixId);
                return false;
            }
            
            spdlog::info("Successfully cleared working_set_id for mix {}", mixId);
            return true;
        }

        bool SqliteMixManager::updateMixTrack(MixId mixId, const MixTrack& updatedTrack) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                // Serialize the entire MixTrack data to JSON
                json mixDataJson = updatedTrack; // This uses the to_json serializer from MixInfo.h

                SqliteStatement stmt{m_db,
                    "UPDATE MixTracks SET mix_data = ? WHERE mix_id = ? AND track_id = ?;"};
                
                stmt.addParam(mixDataJson.dump());
                stmt.addParam(mixId);
                stmt.addParam(updatedTrack.trackId);

                if (!stmt.execute())
                {
                    spdlog::error("Failed to update MixTrack for mix {} track {}", mixId, updatedTrack.trackId);
                    return transaction.rollback();
                }
                spdlog::info("Successfully updated MixTrack for mix {} track {}", mixId, updatedTrack.trackId);
                return transaction.commit();
            }
            return false;
        }
        
        bool SqliteMixManager::setMixStatus(MixId mixId, std::string_view status) const
        {
            SqliteStatement stmt{m_db, "UPDATE Mixes SET status = ? WHERE mix_id = ?;"};
            stmt.addParam(std::string(status));
            stmt.addParam(mixId);
            
            if (!stmt.execute())
            {
                spdlog::error("Failed to set status '{}' for mix {}", status, mixId);
                return false;
            }
            
            spdlog::info("Successfully set status '{}' for mix {}", status, mixId);
            return true;
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
                const auto trackPath{trackInfo.reconstructFullPath()};

                MixTrack mixTrack{};
                mixTrack.mixId = mixInfo.mixId;
                mixTrack.trackId = trackInfo.trackId;
                mixTrack.orderInMix = orderInMix++;

                // For the new model:
                // CUE points - use full track (default behavior)
                mixTrack.cueStart = Duration_t{0};
                mixTrack.cueEnd = Duration_t{0}; // 0 means use full track duration
                
                // ATTACH points - define where overlaps happen
                // For short tracks (< 2 * crossfade duration), reduce or eliminate crossfade
                Duration_t effectiveCrossfade = defaultCrossfadeDuration;
                
                if (trackInfo.duration < minimumExpectedSongLength)
                {
                    // For very short tracks (like intros), eliminate crossfade entirely
                    // This ensures the track plays in full
                    effectiveCrossfade = Duration_t{0};
                    spdlog::info("Track {} ({}) is only {} long: using no crossfade",
                        trackInfo.trackId,
                        pathToString(trackPath),
                        durationToString(trackInfo.duration));
                }
                else if (trackInfo.duration < minimumExpectedSongLength * 2)
                {
                    // For medium-short tracks, use a reduced crossfade (10% of track duration)
                    effectiveCrossfade = trackInfo.duration / 10;
                    spdlog::info("Track {} ({}) is {} long: reducing crossfade to {}",
                        trackInfo.trackId,
                        pathToString(trackPath),
                        durationToString(trackInfo.duration),
                        durationToString(effectiveCrossfade));
                }
                
                // Set attach points based on effective crossfade
                mixTrack.attachFrom = effectiveCrossfade;  // Start overlapping after this duration into the track
                mixTrack.attachTo = trackInfo.duration - effectiveCrossfade; // Next track starts this duration before end
                
                // Create envelope points for crossfade
                // These are relative to the track's cue start (which is 0 in this case)
                // Adapt the envelope based on the effective crossfade duration
                
                if (effectiveCrossfade == Duration_t{0})
                {
                    // No crossfade - track plays at full volume throughout
                    mixTrack.envelopePoints = {
                        {Duration_t{0}, VOLUME_NORMALIZATION},           // Start at 100%
                        {trackInfo.duration, VOLUME_NORMALIZATION}       // End at 100%
                    };
                }
                else
                {
                    // Calculate midpoints based on effective crossfade
                    const auto fadeInMidpoint = std::min(Duration_t{2000}, effectiveCrossfade / 2);
                    const auto fadeOutMidpoint = trackInfo.duration - std::min(Duration_t{2000}, effectiveCrossfade / 2);
                    
                    mixTrack.envelopePoints = {
                        {Duration_t{0}, Volume_t{200}},                                        // Start at 20%
                        {fadeInMidpoint, Volume_t{700}},                                       // midpoint: 70%
                        {effectiveCrossfade, VOLUME_NORMALIZATION},                            // crossfade end: 100%
                        {trackInfo.duration - effectiveCrossfade, VOLUME_NORMALIZATION},       // before fade out: 100%
                        {fadeOutMidpoint, Volume_t{700}},                                      // midpoint: 70%
                        {trackInfo.duration, Volume_t{200}}                                    // End at 20%
                    };
                }

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

        bool SqliteMixManager::setMixExported(MixId mixId, std::string_view exportFolder) const
        {
            const auto now = std::chrono::system_clock::now();

            SqliteStatement stmt{m_db,
                "UPDATE Mixes SET export_folder = ?, exported_at = ?, status = 'Exported' WHERE mix_id = ?;"};
            stmt.addParam(std::string{exportFolder});
            stmt.addParam(timestampToInt64(now));
            stmt.addParam(mixId);

            if (!stmt.execute())
            {
                spdlog::error("Failed to set mix {} as exported to folder '{}'", mixId, exportFolder);
                return false;
            }

            spdlog::info("Successfully marked mix {} as exported to folder '{}'", mixId, exportFolder);
            return true;
        }

        bool SqliteMixManager::moveBackToMixes(MixId mixId) const
        {
            SqliteStatement stmt{m_db,
                "UPDATE Mixes SET export_folder = NULL, status = 'Modified' WHERE mix_id = ?;"};
            stmt.addParam(mixId);

            if (!stmt.execute())
            {
                spdlog::error("Failed to move mix {} back to Mixes", mixId);
                return false;
            }

            spdlog::info("Successfully moved mix {} back to Mixes for editing", mixId);
            return true;
        }

        bool SqliteMixManager::isExported(MixId mixId) const
        {
            SqliteStatement stmt{m_db, "SELECT export_folder FROM Mixes WHERE mix_id = ?;"};
            stmt.addParam(mixId);

            if (stmt.getNextResult())
            {
                return !stmt.isNull(0);  // If export_folder is not NULL, mix is exported
            }

            spdlog::warn("Mix {} not found when checking export status", mixId);
            return false;
        }

        std::vector<ExportFolderInfo> SqliteMixManager::getExportFolders() const
        {
            std::vector<ExportFolderInfo> folders;

            SqliteStatement stmt{m_db,
                "SELECT folder_id, name, display_order, created_at, description "
                "FROM ExportFolders ORDER BY display_order, name;"};

            while (stmt.getNextResult())
            {
                ExportFolderInfo info;
                info.folderId = stmt.getInt32(0);
                info.name = stmt.getText(1);
                info.displayOrder = stmt.getInt32(2);
                const auto timestamp = stmt.getInt64(3);
                info.createdAt = std::chrono::system_clock::time_point(
                    std::chrono::system_clock::duration(timestamp));
                if (!stmt.isNull(4))
                    info.description = stmt.getText(4);

                folders.push_back(info);
            }

            return folders;
        }

        bool SqliteMixManager::createExportFolder(std::string_view name, std::string_view description) const
        {
            const auto now = std::chrono::system_clock::now().time_since_epoch().count();

            // Get the next display_order value
            SqliteStatement orderStmt{m_db, "SELECT COALESCE(MAX(display_order), 0) + 1 FROM ExportFolders;"};
            int displayOrder = 1;
            if (orderStmt.getNextResult())
            {
                displayOrder = static_cast<int>(orderStmt.getInt64(0));
            }

            SqliteStatement stmt{m_db,
                "INSERT INTO ExportFolders (name, display_order, created_at, description) VALUES (?, ?, ?, ?);"};
            stmt.addParam(name);
            stmt.addParam(displayOrder);
            stmt.addParam(static_cast<int64_t>(now));
            if (description.empty())
                stmt.addNullParam();
            else
                stmt.addParam(description);

            if (!stmt.execute())
            {
                spdlog::error("Failed to create export folder '{}': {}", name, m_db.getLastError());
                return false;
            }

            spdlog::info("Successfully created export folder '{}'", name);
            return true;
        }

        std::vector<MixInfo> SqliteMixManager::getMixesByLocation(std::optional<std::string_view> exportFolder) const
        {
            std::vector<MixInfo> mixes;

            std::string query;
            if (exportFolder.has_value())
            {
                // Get mixes in a specific export folder
                query = "SELECT mix_id, name, timestamp, track_count, total_length, source_ws_id, status, "
                       "exported_at, export_folder FROM Mixes WHERE export_folder = ? ORDER BY exported_at DESC;";
            }
            else
            {
                // Get editable mixes (not exported)
                query = "SELECT mix_id, name, timestamp, track_count, total_length, source_ws_id, status, "
                       "exported_at, export_folder FROM Mixes WHERE export_folder IS NULL ORDER BY timestamp DESC;";
            }

            SqliteStatement stmt{m_db, query};
            if (exportFolder.has_value())
            {
                stmt.addParam(std::string(*exportFolder));
            }

            while (stmt.getNextResult())
            {
                MixInfo mix;
                mix.mixId = stmt.getInt64(0);
                mix.name = stmt.getText(1);
                mix.timestamp = timestampFromInt64(stmt.getInt64(2));
                mix.numberOfTracks = stmt.getInt64(3);
                mix.totalDuration = Duration_t{stmt.getInt64(4)};

                if (!stmt.isNull(5))
                {
                    mix.source_ws_id = stmt.getInt64(5);
                }

                mix.status = stmt.getText(6);

                if (!stmt.isNull(7))
                {
                    mix.exportedAt = timestampFromInt64(stmt.getInt64(7));
                }

                if (!stmt.isNull(8))
                {
                    mix.exportFolder = stmt.getText(8);
                }

                mixes.push_back(mix);
            }

            return mixes;
        }


    } // namespace database
} // namespace jucyaudio
