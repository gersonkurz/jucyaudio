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

        namespace
        {
            /// @brief Do these two describe the same audio?
            ///
            /// Compares only what the renderer consumed: identity, position, and the fields that shape
            /// the sound. Everything a MixTrack holds beyond these is either derived or not part of the
            /// mix. Exact float comparison on the gain is deliberate - both sides parsed the same stored
            /// text, so an unchanged mix yields identical values and anything else is a real edit.
            bool describesSameAudio(const MixTrack &a, const MixTrack &b)
            {
                return a.trackId == b.trackId && a.orderInMix == b.orderInMix && a.cueStart == b.cueStart && a.cueEnd == b.cueEnd &&
                       a.attachFrom == b.attachFrom && a.attachTo == b.attachTo && a.gainAdjustment == b.gainAdjustment &&
                       a.envelopePoints == b.envelopePoints;
            }

            /// @brief Parse a MixRecovery row back into the MixTrack the renderer would have seen.
            ///
            /// Used only to answer "did this change?" - the text itself is stored untouched. Mirrors what
            /// mixTrackFromStatement does for the live table, which is why the same from_json runs here.
            ///
            /// @return Nothing if the stored text will not parse. Deliberately not a defaulted MixTrack:
            ///         one of those would carry the ids and default audio fields, and a rendered track
            ///         with no edits has exactly those - so it could compare equal and wave malformed
            ///         data through into the record.
            ///
            /// An empty mix_data is not a failure. The live table treats it as "defaults apply", and so
            /// does this.
            std::optional<MixTrack> parseForComparison(const MixRecoveryEntry &entry)
            {
                MixTrack track{};
                track.trackId = entry.trackId;
                track.orderInMix = entry.orderInMix;
                if (!entry.mixData.empty())
                {
                    // Caught rather than propagated: a mix that will not parse should refuse the capture,
                    // not abort the export that is calling us.
                    try
                    {
                        json::parse(entry.mixData).get_to(track);
                    }
                    catch (const std::exception &e)
                    {
                        spdlog::warn("[MixRecovery] Could not parse mix_data for track {}: {}", entry.trackId, e.what());
                        return std::nullopt;
                    }
                }
                return track;
            }
        } // namespace

        DbResult SqliteMixManager::captureRecoveryData(MixId mixId, MixRecoveryCapture &result, const std::vector<MixTrack> *renderedTracks) const
        {
            // Immediate, not the default deferred mode: this reads, decides something from what it read,
            // and then writes based on that decision. A deferred transaction would let another thread on
            // this connection - or another process - change the mix in between, so the rows written would
            // not be the rows that were validated.
            SqliteTransaction transaction{m_db, TransactionMode::Immediate};
            if (!transaction)
            {
                return DbResult::failure(DbResultStatus::ErrorDB,
                    "Could not begin a transaction to capture recovery data for mix " + std::to_string(mixId));
            }

            // --- the mix itself ---

            std::string mixName;
            int64_t expectedTrackCount = -1;
            Duration_t totalDuration{0};
            {
                SqliteStatement stmt{m_db};
                // total_length is read here, in the same transaction as everything else, and handed back
                // in the result. A caller looking it up afterwards would be reading a second moment.
                if (!stmt.query(
                        [&mixName, &expectedTrackCount, &totalDuration, &stmt]() -> bool
                        {
                            mixName = stmt.getText(0);
                            expectedTrackCount = stmt.getInt64(1);
                            totalDuration = Duration_t{stmt.getInt64(2)};
                            return true;
                        },
                        "SELECT name, track_count, total_length FROM Mixes WHERE mix_id=?",
                        mixId))
                {
                    const auto error{m_db.getLastError()};
                    transaction.rollback();
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to read mix " + std::to_string(mixId) + ": " + error);
                }
            }

            if (expectedTrackCount < 0)
            {
                transaction.rollback();
                return DbResult::failure(DbResultStatus::ErrorNotFound, "No such mix: " + std::to_string(mixId));
            }

            // --- what it contains, in one query ---

            std::vector<MixRecoveryEntry> loaded;
            // Beside loaded, not inside the block below: both are read by the validation that follows it.
            bool sawOrphan = false;
            {
                SqliteStatement stmt{m_db};
                // Folders is joined here rather than asked of SqliteFolderDatabase, and that is not a
                // stylistic preference: its cache accessors take the cache mutex and then reach for the
                // database mutex, while findOrCreateFolderByPath takes them in the opposite order.
                // Calling into it while this transaction holds the database mutex would sit us on one
                // side of that inversion for the whole transaction. Joining avoids the question, and is
                // one query rather than a lookup per track.
                //
                // LEFT JOIN, plus an explicit orphan flag. The foreign key should make an orphaned
                // MixTracks row impossible, but if one exists an inner join would silently drop it and
                // the count check would then report a missing track that is actually present - a
                // misleading diagnosis for an impossible situation. Keeping the row and flagging it means
                // the refusal below can say what is really wrong.
                //
                // Either way it must not be captured: an orphan contributes to the count and to the
                // position sequence while carrying no metadata at all, so a snapshot containing one could
                // pass validation and overwrite a good record with blanks.
                if (!stmt.query(
                        [&loaded, &sawOrphan, &stmt]() -> bool
                        {
                            MixRecoveryEntry entry;
                            int col = 0;
                            sawOrphan = sawOrphan || (stmt.getInt32(col++) != 0);
                            entry.orderInMix = stmt.getInt32(col++);
                            entry.trackId = stmt.getInt64(col++);
                            entry.mixData = stmt.getText(col++);
                            entry.artistName = stmt.getText(col++);
                            entry.albumTitle = stmt.getText(col++);
                            entry.title = stmt.getText(col++);
                            entry.filename = stmt.getText(col++);
                            entry.folderPath = stmt.getText(col++);
                            entry.duration = Duration_t{stmt.getInt64(col++)};
                            entry.filesizeBytes = stmt.getInt64(col++);
                            entry.bpm = stmt.getInt64(col++);
                            loaded.emplace_back(std::move(entry));
                            return true;
                        },
                        "SELECT (t.track_id IS NULL), mt.order_in_mix, mt.track_id, mt.mix_data, "
                        "COALESCE(t.artist_name,''), COALESCE(t.album_title,''), COALESCE(t.title,''), "
                        "COALESCE(t.filename,''), COALESCE(f.actual_path, f.root_path, ''), "
                        "COALESCE(t.duration,0), COALESCE(t.filesize_bytes,0), COALESCE(t.bpm,0) "
                        "FROM MixTracks mt "
                        "LEFT JOIN Tracks t ON t.track_id = mt.track_id "
                        "LEFT JOIN Folders f ON f.folder_id = t.folder_id "
                        "WHERE mt.mix_id = ? ORDER BY mt.order_in_mix ASC",
                        mixId))
                {
                    const auto error{m_db.getLastError()};
                    transaction.rollback();
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to read tracks of mix " + std::to_string(mixId) + ": " + error);
                }
            }

            // --- is this worth recording? ---

            const auto refuse = [&](std::string reason) -> DbResult
            {
                transaction.rollback();
                spdlog::warn("[MixRecovery] Mix {} ({}) not captured: {}", mixId, mixName, reason);
                result.captured = false;
                result.skipReason = std::move(reason);
                result.entries.clear();
                // Cleared like the rest: a caller reusing one of these across mixes would otherwise get
                // Ok, captured == false, no entries, and a duration left over from the last mix that
                // did succeed - the one field that still looks like an answer.
                result.totalDuration = Duration_t{0};
                return DbResult::success();
            };

            if (loaded.empty() || expectedTrackCount == 0)
            {
                // An exported mix always has tracks, so a mix reporting none has something wrong with it.
                // Writing nothing over a previous record is the one outcome most worth refusing.
                return refuse("the mix reports no tracks");
            }

            if (sawOrphan)
            {
                // Should be unreachable while the MixTracks foreign key is enforced. If it is reached,
                // the mix references a track that no longer exists and no amount of metadata can be
                // recorded for it - so refuse rather than write blanks over whatever is there.
                return refuse("the mix references at least one track that no longer exists");
            }

            if (static_cast<int64_t>(loaded.size()) != expectedTrackCount)
            {
                return refuse(std::format("expected {} tracks but found {}", expectedTrackCount, loaded.size()));
            }

            if (renderedTracks != nullptr)
            {
                // The caller rendered something from this mix and needs the record to match it. The
                // renderer read the mix before it started; this reads it now, and minutes of rendering
                // sit in between with another instance free to edit.
                if (renderedTracks->size() != loaded.size())
                {
                    return refuse(std::format("the mix changed while it was being exported - {} track(s) when the export started, {} now",
                        renderedTracks->size(),
                        loaded.size()));
                }

                for (size_t i = 0; i < loaded.size(); ++i)
                {
                    const auto live = parseForComparison(loaded[i]);
                    if (!live.has_value())
                    {
                        // Unreadable, so there is no way to establish it matches what was rendered.
                        // Recording it would put text we cannot make sense of into the one place meant to
                        // explain a mix later.
                        return refuse(std::format("the stored settings for the track at position {} could not be read", i));
                    }

                    if (!describesSameAudio((*renderedTracks)[i], *live))
                    {
                        return refuse(std::format("the mix changed while it was being exported - track at position {} is not the one that was rendered", i));
                    }
                }
            }

            for (size_t i = 0; i < loaded.size(); ++i)
            {
                if (loaded[i].orderInMix != static_cast<int>(i))
                {
                    // A gap can only come from a row vanishing underneath the application:
                    // removeTracksFromMix renumbers, so ordinary editing never leaves one. This is the
                    // signature of the loss the whole feature exists to survive.
                    return refuse(std::format("track positions are not contiguous - expected {} at index {}, found {}", i, i, loaded[i].orderInMix));
                }
            }

            // --- replace ---

            if (!transaction.execute("DELETE FROM MixRecovery WHERE mix_id=?", mixId))
            {
                const auto error{m_db.getLastError()};
                transaction.rollback();
                return DbResult::failure(DbResultStatus::ErrorDB,
                    "Failed to clear previous recovery data for mix " + std::to_string(mixId) + ": " + error);
            }

            // One timestamp for the whole capture: these rows describe a single moment, and stamping them
            // individually would suggest they did not.
            const auto capturedAt = std::chrono::system_clock::now();
            const auto capturedAtMillis = timestampToInt64(capturedAt);

            for (auto &entry : loaded)
            {
                entry.mixId = mixId;
                entry.mixName = mixName;
                entry.capturedAt = capturedAt;

                if (!transaction.execute("INSERT INTO MixRecovery (mix_id, order_in_mix, captured_at, mix_name, track_id, artist_name, "
                                         "album_title, title, filename, folder_path, duration, filesize_bytes, bpm, mix_data) "
                                         "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                        mixId,
                        entry.orderInMix,
                        capturedAtMillis,
                        mixName,
                        entry.trackId,
                        entry.artistName,
                        entry.albumTitle,
                        entry.title,
                        entry.filename,
                        entry.folderPath,
                        static_cast<int64_t>(entry.duration.count()),
                        entry.filesizeBytes,
                        entry.bpm,
                        entry.mixData))
                {
                    const auto error{m_db.getLastError()};
                    transaction.rollback();
                    return DbResult::failure(DbResultStatus::ErrorDB, "Failed to write recovery data for mix " + std::to_string(mixId) + ": " + error);
                }
            }

            if (!transaction.commit())
            {
                return DbResult::failure(DbResultStatus::ErrorDB, "Failed to commit recovery data for mix " + std::to_string(mixId));
            }

            spdlog::info("[MixRecovery] Captured {} track(s) for mix {} ({}).", loaded.size(), mixId, mixName);
            result.captured = true;
            result.skipReason.clear();
            result.entries = std::move(loaded);
            result.totalDuration = totalDuration;
            return DbResult::success();
        }

        DbResult SqliteMixManager::getRecoveryData(MixId mixId, std::vector<MixRecoveryEntry> &entries) const
        {
            // Filled locally and handed over only once the query has completed. query() returns false on
            // a prepare, bind or step failure, and a step failure can land part way through - so
            // assigning as we go would leave the caller holding a truncated snapshot that looks complete.
            std::vector<MixRecoveryEntry> loaded;
            SqliteStatement stmt{m_db};
            const bool queryOk = stmt.query(
                [&loaded, &stmt]() -> bool
                {
                    MixRecoveryEntry entry;
                    int col = 0;
                    entry.mixId = stmt.getInt64(col++);
                    entry.orderInMix = stmt.getInt32(col++);
                    entry.capturedAt = timestampFromInt64(stmt.getInt64(col++));
                    entry.mixName = stmt.getText(col++);
                    entry.trackId = stmt.getInt64(col++);
                    entry.artistName = stmt.getText(col++);
                    entry.albumTitle = stmt.getText(col++);
                    entry.title = stmt.getText(col++);
                    entry.filename = stmt.getText(col++);
                    entry.folderPath = stmt.getText(col++);
                    entry.duration = Duration_t{stmt.getInt64(col++)};
                    entry.filesizeBytes = stmt.getInt64(col++);
                    entry.bpm = stmt.getInt64(col++);
                    entry.mixData = stmt.getText(col++);
                    loaded.emplace_back(std::move(entry));
                    return true;
                },
                // Columns listed rather than SELECT *, because this reads positionally and a later
                // migration that adds a column would otherwise silently shift every field along by one.
                "SELECT mix_id, order_in_mix, captured_at, mix_name, track_id, artist_name, album_title, title, "
                "filename, folder_path, duration, filesize_bytes, bpm, mix_data "
                "FROM MixRecovery WHERE mix_id=? ORDER BY order_in_mix ASC",
                mixId);

            if (!queryOk)
            {
                // Deliberately not "no snapshot": the caller must be able to tell a mix that was never
                // captured from one we simply could not read.
                return DbResult::failure(DbResultStatus::ErrorDB, "Failed to read recovery data for mix " + std::to_string(mixId) + ": " + m_db.getLastError());
            }

            // No undo bookkeeping here, unlike getMixTracks below: this is a record of the past, not a
            // state anyone can edit or would want restored by an undo.
            entries = std::move(loaded);
            return DbResult::success();
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

        bool SqliteMixManager::updateMixSummary(MixId mixId, int64_t trackCount, Duration_t totalLength) const
        {
            SqliteStatement stmt{m_db, "UPDATE Mixes SET track_count = ?, total_length = ? WHERE mix_id = ?;"};
            stmt.addParam(trackCount);
            stmt.addParam(durationToInt64(totalLength));
            stmt.addParam(mixId);

            if (!stmt.execute())
            {
                spdlog::error("Failed to update summary for mix {}: {}", mixId, m_db.getLastError());
                return false;
            }
            return true;
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

                // Step 0: Identify the track being moved by its track_id (stable primary key)
                // before shifting neighbors, since the shift will create an order_in_mix collision.
                SqliteStatement selectStmt{m_db,
                    "SELECT track_id FROM MixTracks WHERE mix_id = ? AND order_in_mix = ?"};
                selectStmt.addParam(mixId);
                selectStmt.addParam(currentOrderInMix);
                int64_t movedTrackId = -1;
                if (selectStmt.getNextResult())
                {
                    movedTrackId = selectStmt.getInt64(0);
                }
                if (movedTrackId < 0)
                {
                    spdlog::error("Could not find track at order {} in mix {}", currentOrderInMix, mixId);
                    return false;
                }

                // Step 1: Shift neighbors to open a gap.
                // If moving UP (newOrderInMix < currentOrder):
                //    - INCREMENT order_in_mix for all tracks in range [newOrderInMix, currentOrder)
                // If moving DOWN (newOrderInMix > currentOrder):
                //    - DECREMENT order_in_mix for all tracks in range (currentOrder, newOrderInMix]
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

                // Step 2: Set the moved track's new position using track_id (not order_in_mix,
                // which is now ambiguous after the shift).
                SqliteStatement setOrderStmt{m_db,
                    "UPDATE MixTracks SET order_in_mix = ? WHERE mix_id = ? AND track_id = ?"};
                setOrderStmt.addParam(newOrderInMix);
                setOrderStmt.addParam(mixId);
                setOrderStmt.addParam(movedTrackId);
                if (!setOrderStmt.execute())
                {
                    spdlog::error("Failed to set new order_in_mix for mix {} track {}", mixId, movedTrackId);
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
