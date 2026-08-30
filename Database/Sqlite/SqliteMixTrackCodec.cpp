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

#include <Database/Sqlite/SqliteMixTrackCodec.h>

#include <Database/Sqlite/SqliteStatement.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <format>
#include <string>

namespace jucyaudio
{
    namespace database
    {
        using json = nlohmann::json;

        bool mixTrackFromStatement(const SqliteStatement &stmt, MixTrack &trackOut, std::string *errorOut)
        {
            MixTrack track{};
            int col = 0;
            track.mixId = stmt.getInt64(col++);
            track.trackId = stmt.getInt64(col++);
            track.orderInMix = stmt.getInt32(col++);

            const std::string jsonData = stmt.getText(col++);
            if (!jsonData.empty())
            {
                try
                {
                    // The from_json deserializer in MixInfo.h; it fills in only the fields the JSON
                    // carries - cue, attach, envelope, gain - and leaves the rest as constructed.
                    json::parse(jsonData).get_to(track);
                }
                catch (const std::exception &e)
                {
                    const auto reason = std::format("track {} at position {}: mix_data could not be read: {}", track.trackId, track.orderInMix, e.what());
                    spdlog::error("[MixTrackCodec] mix {}: {}", track.mixId, reason);
                    if (errorOut != nullptr)
                    {
                        *errorOut = reason;
                    }
                    return false;
                }
            }

            trackOut = std::move(track);
            return true;
        }

        bool bindMixTrackToStatement(SqliteStatement &stmt, const MixTrack &track)
        {
            bool ok = true;
            ok &= stmt.addParam(track.mixId);
            ok &= stmt.addParam(track.trackId);
            ok &= stmt.addParam(track.orderInMix);

            // The whole MixTrack, through the to_json serializer in MixInfo.h.
            const json mixDataJson = track;
            ok &= stmt.addParam(mixDataJson.dump());

            if (!ok)
            {
                spdlog::error("[MixTrackCodec] Failed to bind parameters for mix {} track {}", track.mixId, track.trackId);
            }
            return ok;
        }

    } // namespace database
} // namespace jucyaudio
