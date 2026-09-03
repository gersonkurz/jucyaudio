/*
 * This file is part of jucyaudio.
 * Copyright (C) 2025 Gerson Kurz <not@p-nand-q.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <Database/Sqlite/SqliteMixSummary.h>

#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteMixTrackCodec.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Utils/AssortedUtils.h>

#include <spdlog/spdlog.h>

#include <format>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace jucyaudio
{
    namespace database
    {
        namespace
        {
            /// @brief The natural length of every track in a mix.
            ///
            /// One prepared statement reused per distinct track, not a single query with an IN list of
            /// bound parameters: SQLite caps parameter count, and a mix long enough to exceed it would
            /// fail to prepare - leaving every duration unknown and a mix length of zero looking like the
            /// answer. Lookups by primary key are cheap.
            ///
            /// A track that is simply absent is not a failure; it is left out of the map, and the walk
            /// skips it.
            DbResult loadTrackDurations(SqliteDatabase &db, const std::vector<MixTrack> &tracks, std::unordered_map<TrackId, Duration_t> &durations)
            {
                SqliteStatement stmt{db, "SELECT duration FROM Tracks WHERE track_id = ?;"};
                if (!stmt.isValid())
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, "could not prepare the track duration query: " + db.getLastError());
                }

                std::unordered_set<TrackId> lookedUp;
                for (const auto &track : tracks)
                {
                    if (!lookedUp.insert(track.trackId).second)
                    {
                        continue;
                    }

                    stmt.reset();
                    if (!stmt.addParam(track.trackId))
                    {
                        return DbResult::failure(DbResultStatus::ErrorDB, std::format("could not bind track {}", track.trackId));
                    }

                    if (stmt.getNextResult())
                    {
                        durations[track.trackId] = durationFromInt64(stmt.getInt64(0));
                    }
                    else if (stmt.hasError())
                    {
                        // Distinguished from "no such track" on purpose. Treating a failed query as an
                        // absent track would drop it from the walk and commit a shorter mix as the truth.
                        return DbResult::failure(DbResultStatus::ErrorDB, std::format("could not read the duration of track {}: {}", track.trackId, db.getLastError()));
                    }
                }
                return DbResult::success();
            }
        } // namespace

        DbResult readMixTracksChecked(SqliteDatabase &db, MixId mixId, std::vector<MixTrack> &tracksOut)
        {
            SqliteStatement stmt{db,
                std::format("SELECT {} FROM MixTracks WHERE mix_id=? ORDER BY order_in_mix ASC", mixTrackColumnsForDecoding)};
            if (!stmt.isValid() || !stmt.addParam(mixId))
            {
                return DbResult::failure(DbResultStatus::ErrorDB, std::format("could not query the rows of mix {}: {}", mixId, db.getLastError()));
            }

            // Accumulated locally and only handed over at the end. Writing into tracksOut as rows arrive
            // would leave a caller holding a prefix of the mix when a later row fails to decode, and a
            // prefix is indistinguishable from a shorter mix - which the next save would make true.
            std::vector<MixTrack> tracks;
            while (stmt.getNextResult())
            {
                MixTrack track{};
                std::string decodeError;
                if (!mixTrackFromStatement(stmt, track, &decodeError))
                {
                    // A JSON parse failure, which never reaches SqliteDatabase::getLastError - so the
                    // reason has to be carried out from here or the report says nothing useful.
                    return DbResult::failure(DbResultStatus::ErrorDB, std::format("mix {}: {}", mixId, decodeError));
                }
                tracks.push_back(std::move(track));
            }

            if (stmt.hasError())
            {
                return DbResult::failure(DbResultStatus::ErrorDB, std::format("reading mix {} failed part way through: {}", mixId, db.getLastError()));
            }

            tracksOut = std::move(tracks);
            return DbResult::success();
        }

        DbResult computeMixSummary(SqliteDatabase &db, MixId mixId, MixSummary &summaryOut)
        {
            std::vector<MixTrack> tracks;
            if (const auto read = readMixTracksChecked(db, mixId, tracks); !read.isOk())
            {
                return read;
            }

            std::unordered_map<TrackId, Duration_t> durations;
            if (const auto loaded = loadTrackDurations(db, tracks, durations); !loaded.isOk())
            {
                return loaded;
            }

            summaryOut.trackCount = static_cast<int64_t>(tracks.size());
            summaryOut.totalLength = calculateMixDuration(tracks,
                [&durations](TrackId trackId) -> std::optional<Duration_t>
                {
                    const auto it = durations.find(trackId);
                    if (it == durations.end())
                    {
                        return std::nullopt;
                    }
                    return it->second;
                });
            return DbResult::success();
        }

        DbResult refreshMixSummary(SqliteDatabase &db, SqliteTransaction &transaction, MixId mixId, MixSummary *summaryOut)
        {
            MixSummary summary;
            if (const auto computed = computeMixSummary(db, mixId, summary); !computed.isOk())
            {
                spdlog::error("Could not recompute the summary of mix {}: {}", mixId, computed.errorMessage);
                return computed;
            }

            if (!transaction.execute("UPDATE Mixes SET track_count = ?, total_length = ? WHERE mix_id = ?;",
                    summary.trackCount,
                    durationToInt64(summary.totalLength),
                    mixId))
            {
                return DbResult::failure(DbResultStatus::ErrorDB, std::format("could not store the summary of mix {}: {}", mixId, db.getLastError()));
            }

            if (summaryOut != nullptr)
            {
                *summaryOut = summary;
            }
            return DbResult::success();
        }

        DbResult findMixesContainingTracks(SqliteDatabase &db, const std::vector<TrackId> &trackIds, std::vector<MixId> &mixIdsOut)
        {
            if (trackIds.empty())
            {
                return DbResult::success();
            }

            SqliteStatement stmt{db, "SELECT DISTINCT mix_id FROM MixTracks WHERE track_id = ?;"};
            if (!stmt.isValid())
            {
                return DbResult::failure(DbResultStatus::ErrorDB, "could not prepare the affected-mix query: " + db.getLastError());
            }

            std::unordered_set<MixId> seen;
            for (const auto trackId : trackIds)
            {
                stmt.reset();
                if (!stmt.addParam(trackId))
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, std::format("could not bind track {}", trackId));
                }

                while (stmt.getNextResult())
                {
                    const auto mixId = static_cast<MixId>(stmt.getInt64(0));
                    if (seen.insert(mixId).second)
                    {
                        mixIdsOut.push_back(mixId);
                    }
                }

                if (stmt.hasError())
                {
                    return DbResult::failure(DbResultStatus::ErrorDB, std::format("could not find the mixes holding track {}: {}", trackId, db.getLastError()));
                }
            }
            return DbResult::success();
        }

    } // namespace database
} // namespace jucyaudio
