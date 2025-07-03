#include <Database/Includes/Constants.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteMixManager.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <Utils/AssortedUtils.h>
#include <Utils/StringWriter.h>
#include <spdlog/spdlog.h>

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
        info.silenceStart = durationFromInt64(stmt.getInt64(col++));
        info.fadeInStart = durationFromInt64(stmt.getInt64(col++));
        info.fadeInEnd = durationFromInt64(stmt.getInt64(col++));
        info.fadeOutStart = durationFromInt64(stmt.getInt64(col++));
        info.fadeOutEnd = durationFromInt64(stmt.getInt64(col++));
        info.cutoffTime = durationFromInt64(stmt.getInt64(col++));
        info.volumeAtStart = stmt.getInt64(col++);
        info.volumeAtEnd = stmt.getInt64(col++);
        info.mixStartTime = durationFromInt64(stmt.getInt64(col++));
        info.crossfadeDuration = durationFromInt64(stmt.getInt64(col++));
        return info;
    }

    bool bindMixTrackToStatement(SqliteStatement &stmt, const MixTrack &info)
    {
        bool ok = true;
        ok &= stmt.addParam(info.mixId);   // For the WHERE mix_id = AND
        ok &= stmt.addParam(info.trackId); // track_id = ?
        ok &= stmt.addParam(info.orderInMix);
        ok &= stmt.addParam(durationToInt64(info.silenceStart));
        ok &= stmt.addParam(durationToInt64(info.fadeInStart));
        ok &= stmt.addParam(durationToInt64(info.fadeInEnd));
        ok &= stmt.addParam(durationToInt64(info.fadeOutStart));
        ok &= stmt.addParam(durationToInt64(info.fadeOutEnd));
        ok &= stmt.addParam(durationToInt64(info.cutoffTime));
        ok &= stmt.addParam(info.volumeAtStart);
        ok &= stmt.addParam(info.volumeAtEnd);
        ok &= stmt.addParam(durationToInt64(info.mixStartTime));
        ok &= stmt.addParam(durationToInt64(info.crossfadeDuration));

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
                COUNT(mt.track_id) as track_count,
                MAX(mt.mix_start_time + mt.cutoff_time) as total_length
            FROM Mixes m
            LEFT JOIN MixTracks mt ON m.mix_id = mt.mix_id
            GROUP BY m.mix_id, m.name, m.timestamp)SQL";

            StringWriter output;
            output.append(BASE_STMT);
            if (!args.searchTerms.empty())
            {
                output.append(" WHERE ");
                bool first = true;
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
            if (!args.sortBy.empty())
            {
                output.append(" ORDER BY ");
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
            const auto sql_statement = output.as_string();
            spdlog::info("Executing SQL statement to get mixes: {}", sql_statement);
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
                "SELECT * FROM MixTracks WHERE mix_id=? ORDER BY order_in_mix ASC", mixId);
            return mixTracks;
        }

        bool SqliteMixManager::createOrUpdateMix(MixInfo &mixInfo, std::vector<MixTrack> &tracks) const
        {
            if (SqliteTransaction transaction{m_db})
            {
                mixInfo.timestamp = std::chrono::system_clock::now();

                // if mixId is not 0, the mix already exists, so we update it - by first removing all existing data
                if (mixInfo.mixId)
                {
                    if (!transaction.execute("DELETE FROM MixTracks WHERE mix_id = ?;", mixInfo.mixId) ||
                        !transaction.execute("UPDATE Mixes SET name=?, timestamp=? WHERE mix_id=?", mixInfo.name, timestampToInt64(mixInfo.timestamp),
                                             mixInfo.mixId))
                    {
                        return transaction.rollback();
                    }
                }
                else
                {
                    if (!transaction.execute("INSERT INTO Mixes (name, timestamp) VALUES (?, ?)", mixInfo.name, timestampToInt64(mixInfo.timestamp)))
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
                                                "INSERT INTO MixTracks (mix_id,track_id,order_in_mix,silence_start,fade_in_start,fade_in_end,"
                                                "fade_out_start,fade_out_end,cutoff_time,volume_at_start,volume_at_end,mix_start_time,crossfade_duration) "
                                                "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)"};
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

        bool SqliteMixManager::createAndSaveAutoMix(const std::vector<TrackInfo> &trackInfos,
                                                    /*in/out*/ MixInfo &mixInfo,
                                                    /*out*/ std::vector<MixTrack> &resultingTracks, const Duration_t defaultCrossfadeDuration) const
        {
            assert(resultingTracks.empty() && "resultingTracks should be empty before creating a new mix");
            assert(!trackInfos.empty() && "trackInfos should not be empty when creating a new mix");

            const auto minimumExpectedSongLength = 2 * defaultCrossfadeDuration; // Minimum length for a track to be suitable for mixing
            spdlog::debug("Creating new mix with {} tracks, minimum expected song length: {}", trackInfos.size(), durationToString(minimumExpectedSongLength));

            int orderInMix = 0;
            Duration_t totalDuration = Duration_t::zero();
            for (const auto &trackInfo : trackInfos)
            {
                assert(trackInfo.trackId != 0 && "Track ID should not be zero when creating a new mix");

                // check track is longer than defaultCrossfadeDuration seconds - otherwise it's not suitable for mixing
                if (trackInfo.duration < minimumExpectedSongLength)
                {
                    spdlog::debug("Track {} ({}) is only {} long: too short for mixing, skipping", trackInfo.trackId, pathToString(trackInfo.filepath),
                                  durationToString(trackInfo.duration));
                    continue;
                }

                MixTrack mixTrack;
                mixTrack.mixId = mixInfo.mixId;
                mixTrack.trackId = trackInfo.trackId;
                mixTrack.orderInMix = orderInMix++;
                mixTrack.silenceStart = Duration_t::zero();
                mixTrack.fadeInStart = Duration_t::zero();
                mixTrack.fadeInEnd = defaultCrossfadeDuration;
                mixTrack.fadeOutStart = trackInfo.duration - defaultCrossfadeDuration;
                mixTrack.fadeOutEnd = trackInfo.duration;
                mixTrack.cutoffTime = trackInfo.duration;
                // TBD: need to understand better volume normalization - Gemini, how should we handle this?
                mixTrack.volumeAtStart = 1 * VOLUME_NORMALIZATION;
                mixTrack.volumeAtEnd = 1 * VOLUME_NORMALIZATION;
                if (totalDuration >= defaultCrossfadeDuration)
                {
                    mixTrack.mixStartTime = totalDuration - defaultCrossfadeDuration;
                }
                else
                {
                    mixTrack.mixStartTime = Duration_t::zero();
                }

                mixTrack.crossfadeDuration = defaultCrossfadeDuration;
                resultingTracks.emplace_back(mixTrack);
                totalDuration = mixTrack.mixStartTime + trackInfo.duration;
            }

            // ok, this is the definition; store it in the database
            return createOrUpdateMix(mixInfo, resultingTracks);
        }
    } // namespace database
} // namespace jucyaudio
