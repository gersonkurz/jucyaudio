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
        info.envelopePoints = envelopePointsFromJson(stmt.getText(col++));
        info.mixStartTime = durationFromInt64(stmt.getInt64(col++));
        info.mixEndTime = durationFromInt64(stmt.getInt64(col++));
        return info;
    }

    bool bindMixTrackToStatement(SqliteStatement &stmt, const MixTrack &info)
    {
        bool ok = true;
        ok &= stmt.addParam(info.mixId);   // For the WHERE mix_id = AND
        ok &= stmt.addParam(info.trackId); // track_id = ?
        ok &= stmt.addParam(info.orderInMix);
        ok &= stmt.addParam(envelopePointsToJson(info.envelopePoints));
        ok &= stmt.addParam(durationToInt64(info.mixStartTime));
        ok &= stmt.addParam(durationToInt64(info.mixEndTime));

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
    m.total_length
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
                    output.append("m.name LIKE '%");
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
                "SELECT * FROM MixTracks WHERE mix_id=? ORDER BY order_in_mix ASC", mixId);
            return mixTracks;
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
                if (tracks.empty())
                {
                    mixInfo.totalDuration = Duration_t::zero();
                }
                else
                {
                    const auto lastTrack{tracks.back()};
                    mixInfo.totalDuration = lastTrack.mixEndTime;
                }

                // if mixId is not 0, the mix already exists, so we update it - by first removing all existing data
                if (mixInfo.mixId)
                {
                    if (!transaction.execute("DELETE FROM MixTracks WHERE mix_id = ?;", mixInfo.mixId) ||
                        !transaction.execute("UPDATE Mixes SET name=?, timestamp=?, track_count=?, total_length=? WHERE mix_id=?", mixInfo.name,
                                             timestampToInt64(mixInfo.timestamp), mixInfo.mixId, mixInfo.numberOfTracks,
                                             durationToInt64(mixInfo.totalDuration)))
                    {
                        return transaction.rollback();
                    }
                }
                else
                {
                    if (!transaction.execute("INSERT INTO Mixes (name, timestamp, track_count, total_length) VALUES (?, ?, ?, ?)", mixInfo.name,
                                             timestampToInt64(mixInfo.timestamp), mixInfo.numberOfTracks, durationToInt64(mixInfo.totalDuration)))
                    {   
                        return transaction.rollback();
                    }
                    mixInfo.mixId = m_db.getLastInsertRowId(); // Get the new mix ID
                }
                assert(mixInfo.mixId != 0 && "Mix ID should be set after insert/update");

                for (auto &track : tracks)
                {
                    track.mixId = mixInfo.mixId;
                    SqliteStatement stmt_insert{m_db, "INSERT INTO MixTracks (mix_id,track_id,order_in_mix,envelopePoints,mix_start_time, mix_end_time) "
                                                      "VALUES (?,?,?,?,?,?)"};
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

            mixInfo.numberOfTracks = static_cast<int64_t>(trackInfos.size());

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

                MixTrack mixTrack{};
                mixTrack.mixId = mixInfo.mixId;
                mixTrack.trackId = trackInfo.trackId;
                mixTrack.orderInMix = orderInMix++;

                // Create envelope points with smooth fade curve
                // Time values are relative to track start (0 = beginning of track)
                const auto fadeInMidpoint = Duration_t{2000};                       // 2 seconds
                const auto fadeOutMidpoint = trackInfo.duration - Duration_t{2000}; // 2 seconds before end

                mixTrack.envelopePoints = {
                    {Duration_t{0}, Volume_t{200}},                                        // Start at 20% (200/1000)
                    {fadeInMidpoint, Volume_t{700}},                                       // 2s: 70% (700/1000)
                    {defaultCrossfadeDuration, VOLUME_NORMALIZATION},                      // 5s: 100% (1000/1000)
                    {trackInfo.duration - defaultCrossfadeDuration, VOLUME_NORMALIZATION}, // duration-5s: 100%
                    {fadeOutMidpoint, Volume_t{700}},                                      // duration-2s: 70%
                    {trackInfo.duration, Volume_t{200}}                                    // End at 20%
                };

                // Calculate mix start time with crossfade overlap
                if (totalDuration >= defaultCrossfadeDuration)
                {
                    mixTrack.mixStartTime = totalDuration - defaultCrossfadeDuration;
                }
                else
                {
                    mixTrack.mixStartTime = Duration_t::zero();
                }
                mixTrack.mixEndTime = mixTrack.mixStartTime + trackInfo.duration;
                resultingTracks.emplace_back(mixTrack);
                totalDuration = mixTrack.mixEndTime;
            }
            mixInfo.totalDuration = totalDuration;

            // ok, this is the definition; store it in the database
            return createOrUpdateMix(mixInfo, resultingTracks);
        }
    } // namespace database
} // namespace jucyaudio
