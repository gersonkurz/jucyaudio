#include <Database/Includes/Constants.h>
#include <Database/Includes/MixTrackUtils.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteMixManager.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Database/BackgroundTasks/EnergyAnalyzer.h>
#include <Database/BackgroundTasks/TransitionCalculator.h>
#include <Database/TrackLibrary.h>
#include <UI/Settings.h>
#include <Utils/AssortedUtils.h>
#include <Utils/StringWriter.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <Database/UndoManager.h>

using json = nlohmann::json;

namespace
{
    using namespace jucyaudio;
    using namespace jucyaudio::database;

    bool finalizeMixForExportTransaction(SqliteDatabase& db, SqliteTransaction& transaction, MixId mixId)
    {
        SqliteStatement stmt_info{db, "SELECT status, source_ws_id FROM Mixes WHERE mix_id = ?;"};
        stmt_info.addParam(mixId);
        if (!stmt_info.getNextResult())
        {
            spdlog::error("FinalizeMix: Could not find mix with ID {}", mixId);
            return false;
        }

        std::string status = stmt_info.getText(0);
        std::optional<WorkingSetId> source_ws_id;
        if (!stmt_info.isNull(1))
        {
            source_ws_id = stmt_info.getInt64(1);
        }

        if (status != "New" || !source_ws_id.has_value())
        {
            return true;
        }

        SqliteStatement stmt_prune{db,
            "SELECT track_id FROM MixTracks WHERE mix_id = ? AND order_in_mix <= "
            "(SELECT MAX(order_in_mix) FROM MixTracks WHERE mix_id = ?)"};
        stmt_prune.addParam(mixId);
        stmt_prune.addParam(mixId);

        std::vector<TrackId> tracksToPrune;
        while (stmt_prune.getNextResult())
        {
            tracksToPrune.push_back(stmt_prune.getInt64(0));
        }

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

            SqliteStatement stmt_delete{db, delete_query.asString()};
            stmt_delete.addParam(source_ws_id.value());
            if (!stmt_delete.execute())
            {
                spdlog::error("FinalizeMix: Failed to prune tracks from working set {}", source_ws_id.value());
                return false;
            }
        }

        if (!transaction.execute("UPDATE Mixes SET status = 'Finalized', source_ws_id = NULL WHERE mix_id = ?;", mixId))
        {
            spdlog::error("FinalizeMix: Failed to update mix status for mix ID {}", mixId);
            return false;
        }

        return true;
    }

    MixInfo mixInfoFromStatement(const SqliteStatement &stmt)
    {
        MixInfo info{};
        size_t col = 0;
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

    std::vector<MixTrack> loadMixTracksForMix(database::SqliteDatabase &db, MixId mixId)
    {
        std::vector<MixTrack> mixTracks;
        SqliteStatement stmt{db};
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

    bool recalculateNewAdjacenciesAfterTrackRemoval(database::SqliteDatabase &db, MixId mixId, const std::vector<MixTrack> &oldTracks)
    {
        using namespace jucyaudio::database::background_tasks;

        if (!config::theSettings.mixEditingSettings.useSmartAutomix.get())
        {
            return true;
        }

        auto newTracks = loadMixTracksForMix(db, mixId);
        if (newTracks.size() < 2 || oldTracks.empty())
        {
            return true;
        }

        // Count old adjacency pairs (supports duplicate track IDs in a mix)
        std::map<std::pair<TrackId, TrackId>, int> oldAdjacencyCounts;
        for (size_t i = 0; i + 1 < oldTracks.size(); ++i)
        {
            ++oldAdjacencyCounts[{oldTracks[i].trackId, oldTracks[i + 1].trackId}];
        }

        // Identify adjacency boundaries that are new after removal.
        std::vector<size_t> newBoundaryIndices;
        for (size_t i = 0; i + 1 < newTracks.size(); ++i)
        {
            const auto key = std::pair<TrackId, TrackId>{newTracks[i].trackId, newTracks[i + 1].trackId};
            const auto it = oldAdjacencyCounts.find(key);
            if (it != oldAdjacencyCounts.end() && it->second > 0)
            {
                --it->second;
            }
            else
            {
                newBoundaryIndices.push_back(i);
            }
        }

        if (newBoundaryIndices.empty())
        {
            return true;
        }

        std::unordered_map<TrackId, TrackInfo> trackInfoCache;
        auto getTrackInfo = [&trackInfoCache](TrackId trackId) -> std::optional<TrackInfo>
        {
            if (const auto it = trackInfoCache.find(trackId); it != trackInfoCache.end())
            {
                return it->second;
            }

            const auto trackOpt = theTrackLibrary.getTrackById(trackId);
            if (!trackOpt)
            {
                return std::nullopt;
            }

            trackInfoCache.emplace(trackId, *trackOpt);
            return trackOpt;
        };

        const bool linkEnvelopePoints = config::theSettings.mixEditingSettings.linkEnvelopePointsToAttachPoints.get();
        std::set<size_t> changedTrackIndices;

        for (const auto boundaryIndex : newBoundaryIndices)
        {
            auto &trackA = newTracks[boundaryIndex];
            auto &trackB = newTracks[boundaryIndex + 1];

            const auto trackAInfoOpt = getTrackInfo(trackA.trackId);
            const auto trackBInfoOpt = getTrackInfo(trackB.trackId);
            if (!trackAInfoOpt || !trackBInfoOpt)
            {
                spdlog::warn("Smart recalculation skipped for pair {} -> {}: track info missing",
                    trackA.trackId, trackB.trackId);
                continue;
            }

            const auto energyA = EnergyAnalyzer::getCachedData(*trackAInfoOpt);
            const auto energyB = EnergyAnalyzer::getCachedData(*trackBInfoOpt);
            if (!(energyA && energyA->isValid && energyB && energyB->isValid))
            {
                // User-requested behavior: no fallback; leave unchanged.
                spdlog::info("Smart recalculation skipped for pair {} -> {}: cached energy missing/invalid",
                    trackA.trackId, trackB.trackId);
                continue;
            }

            const auto transition = TransitionCalculator::calculate(
                *energyA, trackAInfoOpt->duration,
                *energyB, trackBInfoOpt->duration);

            const auto oldAttachToA = trackA.attachTo;
            const auto oldAttachFromB = trackB.attachFrom;

            auto newAttachToA = transition.attachToA;
            auto newAttachFromB = transition.attachFromB;

            newAttachToA = std::max(Duration_t{0}, std::min(newAttachToA, trackAInfoOpt->duration));
            newAttachFromB = std::max(Duration_t{0}, std::min(newAttachFromB, trackBInfoOpt->duration));

            if (newAttachToA <= trackA.attachFrom)
            {
                newAttachToA = trackAInfoOpt->duration;
            }
            if (newAttachFromB >= trackB.attachTo)
            {
                newAttachFromB = Duration_t{0};
            }

            if (newAttachToA != oldAttachToA)
            {
                if (linkEnvelopePoints)
                {
                    trackA.scaleEnvelopePointsForAttachChange(
                        trackA.attachFrom,
                        trackA.attachFrom,
                        oldAttachToA,
                        newAttachToA,
                        trackAInfoOpt->duration);
                }
                trackA.attachTo = newAttachToA;
                changedTrackIndices.insert(boundaryIndex);
            }

            if (newAttachFromB != oldAttachFromB)
            {
                if (linkEnvelopePoints)
                {
                    trackB.scaleEnvelopePointsForAttachChange(
                        oldAttachFromB,
                        newAttachFromB,
                        trackB.attachTo,
                        trackB.attachTo,
                        trackBInfoOpt->duration);
                }
                trackB.attachFrom = newAttachFromB;
                changedTrackIndices.insert(boundaryIndex + 1);
            }

            if (newAttachToA != oldAttachToA || newAttachFromB != oldAttachFromB)
            {
                spdlog::info("Smart recalculation {} -> {}: attachTo {}ms -> {}ms, attachFrom {}ms -> {}ms",
                    trackA.trackId, trackB.trackId,
                    oldAttachToA.count(), newAttachToA.count(),
                    oldAttachFromB.count(), newAttachFromB.count());
            }
        }

        if (changedTrackIndices.empty())
        {
            return true;
        }

        SqliteStatement updateStmt{db, "UPDATE MixTracks SET mix_data=? WHERE mix_id=? AND order_in_mix=?;"};
        for (const auto trackIndex : changedTrackIndices)
        {
            const auto &track = newTracks[trackIndex];
            json mixDataJson = track;

            updateStmt.addParam(mixDataJson.dump());
            updateStmt.addParam(mixId);
            updateStmt.addParam(track.orderInMix);
            if (!updateStmt.execute())
            {
                spdlog::error("Failed to persist recalculated transition data for mix {}, order {}", mixId, track.orderInMix);
                return false;
            }
            updateStmt.reset();
        }

        return true;
    }
} // namespace

namespace jucyaudio
{
    namespace database
    {
        MixInfo SqliteMixManager::getMix(MixId mixId) const
        {
            // Direct query without export_folder filtering - need to load any mix regardless of export status
            SqliteStatement stmt{m_db, R"SQL(SELECT
                mix_id, name, timestamp, track_count, total_length, source_ws_id, status, exported_at, export_folder
                FROM Mixes WHERE mix_id = ?;)SQL"};

            stmt.addParam(mixId);

            if (stmt.getNextResult())
            {
                return mixInfoFromStatement(stmt);
            }

            return MixInfo{}; // Return empty if not found
        }

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

            // special case: record first ever mix
            if (!theUndoManager.isMixKnown(mixId) && !mixTracks.empty())
            {
                ExtendedMixInfo extMixInfo;
                extMixInfo.mixInfo = getMix(mixId);
                extMixInfo.tracks = mixTracks;
                theUndoManager.recordMixChange(std::move(extMixInfo));
            }
            return mixTracks;
        }

        int SqliteMixManager::getTrackCountForMix(MixId mixId) const
        {
            int count = 0;
            SqliteStatement stmt{m_db};
            stmt.query(
                [&count, &stmt]() -> bool
                {
                    count = stmt.getInt32(0);
                    return true;
                },
                "SELECT COUNT(*) FROM MixTracks WHERE mix_id=?",
                mixId);
            return count;
        }

        bool SqliteMixManager::removeTracksFromMix(MixId mixId, const std::vector<TrackId> &trackIds) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                const auto oldTracks = loadMixTracksForMix(m_db, mixId);

                // Resolve concrete row instances (order_in_mix) from the pre-delete snapshot.
                // This is critical when the same track_id appears multiple times in one mix.
                std::vector<int> deletedOrders;
                std::unordered_map<TrackId, int> requestedCounts;
                for (const auto trackId : trackIds)
                {
                    requestedCounts[trackId]++;
                }
                for (const auto &track : oldTracks)
                {
                    auto it = requestedCounts.find(track.trackId);
                    if (it != requestedCounts.end() && it->second > 0)
                    {
                        deletedOrders.push_back(track.orderInMix);
                        --it->second;
                    }
                }
                
                // Sort the orders so we can calculate the shift correctly
                std::sort(deletedOrders.begin(), deletedOrders.end());
                
                // Delete each concrete row by order_in_mix to avoid deleting all duplicates.
                SqliteStatement stmt{m_db, "DELETE FROM MixTracks WHERE mix_id = ? AND order_in_mix = ?"};
                for (const auto deletedOrder : deletedOrders)
                {
                    stmt.addParam(mixId);
                    stmt.addParam(deletedOrder);
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
                    spdlog::info("Re-enumerated orderInMix after deleting {} tracks", deletedOrders.size());
                }

                if (!recalculateNewAdjacenciesAfterTrackRemoval(m_db, mixId, oldTracks))
                {
                    spdlog::error("Failed to recalculate transitions after batch track deletion");
                    return transaction.rollback();
                }
                
                if (transaction.commit())
                {
                    recordMixChange(mixId);
                    return true;
                }
                transaction.rollback();
            }
            return false;
        }

        bool SqliteMixManager::removeTrackFromMix(MixId mixId, TrackId trackId) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                const auto oldTracks = loadMixTracksForMix(m_db, mixId);

                // Pick one concrete instance (lowest order) when duplicates exist.
                SqliteStatement getOrderStmt{m_db, "SELECT order_in_mix FROM MixTracks WHERE mix_id = ? AND track_id = ? ORDER BY order_in_mix LIMIT 1"};
                getOrderStmt.addParam(mixId);
                getOrderStmt.addParam(trackId);
                
                int deletedTrackOrder = -1;
                if (getOrderStmt.getNextResult())
                {
                    deletedTrackOrder = getOrderStmt.getInt32(0);
                }
                
                if (deletedTrackOrder < 0)
                {
                    spdlog::warn("Track {} not found in mix {}, nothing to delete", trackId, mixId);
                    return transaction.rollback();
                }

                // Delete exactly one row instance.
                SqliteStatement stmt{m_db, "DELETE FROM MixTracks WHERE mix_id = ? AND order_in_mix = ?"};
                stmt.addParam(mixId);
                stmt.addParam(deletedTrackOrder);
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

                if (!recalculateNewAdjacenciesAfterTrackRemoval(m_db, mixId, oldTracks))
                {
                    spdlog::error("Failed to recalculate transitions after track deletion");
                    return transaction.rollback();
                }
                
                if (transaction.commit())
                {
                    recordMixChange(mixId);
                    return true;
                }
                transaction.rollback();
            }
            return false;
        }

        bool SqliteMixManager::removeTrackFromMixAtOrder(MixId mixId, int orderInMix) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                const auto oldTracks = loadMixTracksForMix(m_db, mixId);

                const auto trackIt = std::find_if(
                    oldTracks.begin(),
                    oldTracks.end(),
                    [orderInMix](const MixTrack &track)
                    {
                        return track.orderInMix == orderInMix;
                    });

                if (trackIt == oldTracks.end())
                {
                    spdlog::warn("Track order {} not found in mix {}, nothing to delete", orderInMix, mixId);
                    return transaction.rollback();
                }

                SqliteStatement stmt{m_db, "DELETE FROM MixTracks WHERE mix_id = ? AND order_in_mix = ?"};
                stmt.addParam(mixId);
                stmt.addParam(orderInMix);
                if (!stmt.execute())
                {
                    return transaction.rollback();
                }

                SqliteStatement updateStmt{m_db, "UPDATE MixTracks SET order_in_mix = order_in_mix - 1 WHERE mix_id = ? AND order_in_mix > ?"};
                updateStmt.addParam(mixId);
                updateStmt.addParam(orderInMix);
                if (!updateStmt.execute())
                {
                    spdlog::error("Failed to re-enumerate orderInMix after track deletion");
                    return transaction.rollback();
                }

                if (!recalculateNewAdjacenciesAfterTrackRemoval(m_db, mixId, oldTracks))
                {
                    spdlog::error("Failed to recalculate transitions after track deletion");
                    return transaction.rollback();
                }

                if (transaction.commit())
                {
                    recordMixChange(mixId);
                    return true;
                }
                transaction.rollback();
            }
            return false;
        }

        bool SqliteMixManager::reorderTrackInMix(MixId mixId, int currentOrderInMix, int newOrderInMix) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                // If the position hasn't changed, nothing to do
                if (currentOrderInMix == newOrderInMix)
                {
                    return true;
                }

                // Validate newOrderInMix is within bounds
                SqliteStatement countStmt{m_db, "SELECT COUNT(*) FROM MixTracks WHERE mix_id = ?"};
                countStmt.addParam(mixId);
                int trackCount = 0;
                if (countStmt.getNextResult())
                {
                    trackCount = countStmt.getInt32(0);
                }

                if (currentOrderInMix < 0 || currentOrderInMix >= trackCount)
                {
                    spdlog::error("Invalid currentOrderInMix {} for mix {} with {} tracks", currentOrderInMix, mixId, trackCount);
                    return false;
                }

                if (newOrderInMix < 0 || newOrderInMix >= trackCount)
                {
                    spdlog::error("Invalid newOrderInMix {} for mix {} with {} tracks", newOrderInMix, mixId, trackCount);
                    return false;
                }

                // Move the track:
                // 1. If moving UP (newOrderInMix < currentOrder):
                //    - INCREMENT order_in_mix for all tracks in range [newOrderInMix, currentOrder)
                // 2. If moving DOWN (newOrderInMix > currentOrder):
                //    - DECREMENT order_in_mix for all tracks in range (currentOrder, newOrderInMix]
                // 3. Set the moved track's order_in_mix = newOrderInMix

                if (newOrderInMix < currentOrderInMix)
                {
                    // Moving UP - shift tracks down
                    SqliteStatement updateStmt{m_db,
                        "UPDATE MixTracks SET order_in_mix = order_in_mix + 1 "
                        "WHERE mix_id = ? AND order_in_mix >= ? AND order_in_mix < ?"};
                    updateStmt.addParam(mixId);
                    updateStmt.addParam(newOrderInMix);
                    updateStmt.addParam(currentOrderInMix);
                    if (!updateStmt.execute())
                    {
                        spdlog::error("Failed to shift tracks for reorder (moving up)");
                        return transaction.rollback();
                    }
                }
                else // newOrderInMix > currentOrder
                {
                    // Moving DOWN - shift tracks up
                    SqliteStatement updateStmt{m_db,
                        "UPDATE MixTracks SET order_in_mix = order_in_mix - 1 "
                        "WHERE mix_id = ? AND order_in_mix > ? AND order_in_mix <= ?"};
                    updateStmt.addParam(mixId);
                    updateStmt.addParam(currentOrderInMix);
                    updateStmt.addParam(newOrderInMix);
                    if (!updateStmt.execute())
                    {
                        spdlog::error("Failed to shift tracks for reorder (moving down)");
                        return transaction.rollback();
                    }
                }

                // Finally, set the moved track's new position
                SqliteStatement setOrderStmt{m_db, "UPDATE MixTracks SET order_in_mix = ? WHERE mix_id = ? AND order_in_mix = ?"};
                setOrderStmt.addParam(newOrderInMix);
                setOrderStmt.addParam(mixId);
                setOrderStmt.addParam(currentOrderInMix);
                if (!setOrderStmt.execute())
                {
                    spdlog::error("Failed to set new order_in_mix for mix {} row {}", mixId, currentOrderInMix);
                    return transaction.rollback();
                }

                spdlog::info("Reordered mix {} row from position {} to {}", mixId, currentOrderInMix, newOrderInMix);

                if (transaction.commit())
                {
                    recordMixChange(mixId);
                    return true;
                }
                transaction.rollback();
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
                if (transaction.commit())
                {
                    recordMixChange(mixInfo.mixId);
                    return true;
                }
                transaction.rollback();
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
                if (!finalizeMixForExportTransaction(m_db, transaction, mixId))
                {
                    return transaction.rollback();
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
                if (transaction.commit())
                {
                    recordMixChange(mixId);
                    return true;
                }
                transaction.rollback();
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
            using namespace background_tasks;

            assert(resultingTracks.empty() && "resultingTracks should be empty before creating a new mix");
            assert(!trackInfos.empty() && "trackInfos should not be empty when creating a new mix");

            mixInfo.numberOfTracks = static_cast<int64_t>(trackInfos.size());
            mixInfo.source_ws_id = source_ws_id;

            spdlog::info("Creating smart automix with {} tracks, default crossfade: {}",
                        trackInfos.size(), durationToString(defaultCrossfadeDuration));

            // --- Phase 1: Retrieve cached energy data for all tracks ---
            // Energy analysis should have been done by EnergyAnalysisTask before this call.
            // If any track is missing data, we'll use fallback transitions for that pair.
            std::vector<EnergyAnalysisResult> energyData;
            energyData.reserve(trackInfos.size());

            const bool useSmartAutomix = config::theSettings.mixEditingSettings.useSmartAutomix.get();
            if (!useSmartAutomix)
            {
                spdlog::info("Smart automix disabled: using fixed crossfade heuristics");
                energyData.resize(trackInfos.size());
            }
            else
            {
                int cachedCount = 0;
                for (const auto& trackInfo : trackInfos)
                {
                    auto cached = EnergyAnalyzer::getCachedData(trackInfo);
                    if (cached && cached->isValid)
                    {
                        spdlog::debug("Using cached energy data for track {}", trackInfo.trackId);
                        energyData.push_back(*cached);
                        ++cachedCount;
                    }
                    else
                    {
                        // No cached data - push invalid result, will trigger fallback for transitions involving this track
                        spdlog::debug("No cached energy data for track {}, will use fallback", trackInfo.trackId);
                        energyData.push_back(EnergyAnalysisResult{});
                    }
                }

                spdlog::info("Energy data: {}/{} tracks have cached analysis", cachedCount, trackInfos.size());
            }

            // --- Phase 2: Calculate transitions between adjacent track pairs ---
            // Store calculated transitions: transitions[i] is between track[i] and track[i+1]
            std::vector<TransitionResult> transitions;
            transitions.reserve(trackInfos.size() > 0 ? trackInfos.size() - 1 : 0);

            for (size_t i = 0; i + 1 < trackInfos.size(); ++i)
            {
                const auto& trackA = trackInfos[i];
                const auto& trackB = trackInfos[i + 1];
                const auto& energyA = energyData[i];
                const auto& energyB = energyData[i + 1];

                TransitionResult transition;
                if (energyA.isValid && energyB.isValid)
                {
                    transition = TransitionCalculator::calculate(
                        energyA, trackA.duration,
                        energyB, trackB.duration);
                    spdlog::info("Smart transition {}->{}: attachTo={}ms, attachFrom={}ms, crossfade={}ms, score={:.2f}",
                                trackA.trackId, trackB.trackId,
                                transition.attachToA.count(), transition.attachFromB.count(),
                                transition.crossfadeDuration.count(), transition.score);
                }
                else
                {
                    transition = TransitionCalculator::calculateFallback(
                        trackA.duration, trackB.duration, defaultCrossfadeDuration);
                    spdlog::info("Fallback transition {}->{}: crossfade={}ms",
                                trackA.trackId, trackB.trackId, transition.crossfadeDuration.count());
                }

                transitions.push_back(transition);
            }

            // --- Phase 3: Build MixTracks with calculated attach points ---
            int orderInMix = 0;
            for (size_t i = 0; i < trackInfos.size(); ++i)
            {
                const auto& trackInfo = trackInfos[i];
                assert(trackInfo.trackId != 0 && "Track ID should not be zero when creating a new mix");

                MixTrack mixTrack{};
                mixTrack.mixId = mixInfo.mixId;
                mixTrack.trackId = trackInfo.trackId;
                mixTrack.orderInMix = orderInMix++;

                // CUE points - use full track
                mixTrack.cueStart = Duration_t{0};
                mixTrack.cueEnd = Duration_t{0};

                // Determine attach points from transitions
                Duration_t attachFrom{0};
                Duration_t attachTo = trackInfo.duration;
                Duration_t crossfadeDuration = defaultCrossfadeDuration;

                // attachFrom comes from the transition with the previous track
                if (i > 0)
                {
                    const auto& prevTransition = transitions[i - 1];
                    attachFrom = prevTransition.attachFromB;
                    crossfadeDuration = prevTransition.crossfadeDuration;
                }

                // attachTo comes from the transition with the next track
                if (i + 1 < trackInfos.size())
                {
                    const auto& nextTransition = transitions[i];
                    attachTo = nextTransition.attachToA;
                    // Use the larger crossfade duration for envelope calculation
                    if (nextTransition.crossfadeDuration > crossfadeDuration)
                        crossfadeDuration = nextTransition.crossfadeDuration;
                }

                // Ensure attach points are valid
                if (attachFrom < Duration_t{0})
                    attachFrom = Duration_t{0};
                if (attachTo > trackInfo.duration)
                    attachTo = trackInfo.duration;
                if (attachTo <= attachFrom)
                    attachTo = trackInfo.duration;

                mixTrack.attachFrom = attachFrom;
                mixTrack.attachTo = attachTo;

                // Generate envelope points based on crossfade duration
                const auto effectiveFadeIn = std::min(crossfadeDuration, attachFrom);
                const auto effectiveFadeOut = std::min(crossfadeDuration, trackInfo.duration - attachTo);

                if (effectiveFadeIn == Duration_t{0} && effectiveFadeOut == Duration_t{0})
                {
                    // No crossfade - full volume throughout
                    mixTrack.envelopePoints = {
                        {Duration_t{0}, VOLUME_NORMALIZATION},
                        {trackInfo.duration, VOLUME_NORMALIZATION}
                    };
                }
                else
                {
                    // Calculate envelope with smooth fade curves
                    const auto fadeInMidpoint = std::min(Duration_t{2000}, effectiveFadeIn / 2);
                    const auto fadeOutStart = attachTo;
                    const auto fadeOutMidpoint = trackInfo.duration - std::min(Duration_t{2000}, effectiveFadeOut / 2);

                    mixTrack.envelopePoints = {
                        {Duration_t{0}, Volume_t{200}},                       // Start at 20%
                        {fadeInMidpoint, Volume_t{700}},                      // Midpoint: 70%
                        {attachFrom, VOLUME_NORMALIZATION},                   // Full volume at attachFrom
                        {fadeOutStart, VOLUME_NORMALIZATION},                 // Full volume until attachTo
                        {fadeOutMidpoint, Volume_t{700}},                     // Midpoint: 70%
                        {trackInfo.duration, Volume_t{200}}                   // End at 20%
                    };
                }

                resultingTracks.emplace_back(mixTrack);
            }

            // --- Phase 4: Calculate total mix duration ---
            if (!resultingTracks.empty())
            {
                Duration_t mixEndPosition{0};
                Duration_t previousTrackStart{0};

                for (size_t i = 0; i < resultingTracks.size(); ++i)
                {
                    const auto& track = resultingTracks[i];
                    const auto& trackInfo = trackInfos[i]; // Same order guaranteed

                    Duration_t trackStart{0};
                    if (i == 0)
                    {
                        trackStart = Duration_t{0};
                    }
                    else
                    {
                        const auto& prevTrack = resultingTracks[i - 1];
                        trackStart = previousTrackStart + prevTrack.attachTo - track.attachFrom;
                    }

                    Duration_t trackEnd = trackStart + trackInfo.duration;
                    if (trackEnd > mixEndPosition)
                        mixEndPosition = trackEnd;

                    previousTrackStart = trackStart;
                }

                mixInfo.totalDuration = mixEndPosition;
            }
            else
            {
                mixInfo.totalDuration = Duration_t{0};
            }

            spdlog::info("Smart automix created: {} tracks, total duration: {}",
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
                    if (transaction.commit())
                    {
                        recordMixChange(mixId);
                        return true;
                    }
                }
                return transaction.rollback();
            }
            return false;
        }

        bool SqliteMixManager::setMixExported(MixId mixId, std::string_view exportFolder) const
        {
            const auto now = std::chrono::system_clock::now();

            SqliteStatement stmt{m_db,
                "UPDATE Mixes SET export_folder = ?, exported_at = ?, status = 'Exported', source_ws_id = NULL WHERE mix_id = ?;"};
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

        bool SqliteMixManager::setPendingExportSettings(MixId mixId, const audio::ActiveExportSettings& settings) const
        {
            const nlohmann::json j = settings;
            SqliteStatement stmt{m_db, "UPDATE Mixes SET pending_export_settings = ? WHERE mix_id = ?;"};
            stmt.addParam(j.dump());
            stmt.addParam(mixId);

            if (!stmt.execute())
            {
                spdlog::error("Failed to set pending export settings for mix {}", mixId);
                return false;
            }
            spdlog::info("Scheduled mix {} for export", mixId);
            return true;
        }

        bool SqliteMixManager::scheduleMixForExport(MixId mixId, const audio::ActiveExportSettings& settings) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                if (!finalizeMixForExportTransaction(m_db, transaction, mixId))
                {
                    return transaction.rollback();
                }

                const nlohmann::json j = settings;
                SqliteStatement stmt{m_db, "UPDATE Mixes SET pending_export_settings = ? WHERE mix_id = ?;"};
                stmt.addParam(j.dump());
                stmt.addParam(mixId);

                if (!stmt.execute())
                {
                    spdlog::error("Failed to schedule mix {} for export", mixId);
                    return transaction.rollback();
                }

                if (!transaction.commit())
                {
                    spdlog::error("Failed to commit scheduled export for mix {}", mixId);
                    return false;
                }

                spdlog::info("Scheduled mix {} for export", mixId);
                return true;
            }

            return false;
        }

        bool SqliteMixManager::clearPendingExportSettings(MixId mixId) const
        {
            SqliteStatement stmt{m_db, "UPDATE Mixes SET pending_export_settings = NULL WHERE mix_id = ?;"};
            stmt.addParam(mixId);

            if (!stmt.execute())
            {
                spdlog::error("Failed to clear pending export settings for mix {}", mixId);
                return false;
            }
            return true;
        }

        std::optional<audio::ActiveExportSettings> SqliteMixManager::getPendingExportSettings(MixId mixId) const
        {
            SqliteStatement stmt{m_db, "SELECT pending_export_settings FROM Mixes WHERE mix_id = ?;"};
            stmt.addParam(mixId);

            if (stmt.getNextResult() && !stmt.isNull(0))
            {
                try
                {
                    const auto j = nlohmann::json::parse(stmt.getText(0));
                    return j.get<audio::ActiveExportSettings>();
                }
                catch (const nlohmann::json::exception& e)
                {
                    spdlog::error("Failed to parse pending export settings for mix {}: {}", mixId, e.what());
                }
            }
            return std::nullopt;
        }

        std::vector<IMixManager::ScheduledExport> SqliteMixManager::getMixesScheduledForExport() const
        {
            std::vector<ScheduledExport> result;

            SqliteStatement stmt{m_db,
                "SELECT mix_id, name, timestamp, track_count, total_length, source_ws_id, status, "
                "exported_at, export_folder, pending_export_settings "
                "FROM Mixes WHERE pending_export_settings IS NOT NULL ORDER BY mix_id ASC;"};

            while (stmt.getNextResult())
            {
                MixInfo mix;
                mix.mixId = stmt.getInt64(0);
                mix.name = stmt.getText(1);
                mix.timestamp = timestampFromInt64(stmt.getInt64(2));
                mix.numberOfTracks = stmt.getInt64(3);
                mix.totalDuration = Duration_t{stmt.getInt64(4)};
                if (!stmt.isNull(5)) mix.source_ws_id = stmt.getInt64(5);
                mix.status = stmt.getText(6);
                if (!stmt.isNull(7)) mix.exportedAt = timestampFromInt64(stmt.getInt64(7));
                if (!stmt.isNull(8)) mix.exportFolder = stmt.getText(8);

                try
                {
                    const auto j = nlohmann::json::parse(stmt.getText(9));
                    auto settings = j.get<audio::ActiveExportSettings>();
                    result.push_back({std::move(mix), std::move(settings)});
                }
                catch (const nlohmann::json::exception& e)
                {
                    spdlog::error("Skipping mix {} with invalid pending export settings: {}", mix.mixId, e.what());
                }
            }

            return result;
        }

    } // namespace database
} // namespace jucyaudio
