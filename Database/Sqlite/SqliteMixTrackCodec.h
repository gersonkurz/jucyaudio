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

#pragma once

#include <Database/Includes/MixInfo.h>

#include <string>

namespace jucyaudio
{
    namespace database
    {
        class SqliteStatement;

        /**
         * @brief Reads one MixTracks row, mix_data included.
         *
         * Shared rather than file-local because more than one place reads that table: the mix manager
         * for the usual operations, and the summary walk that runs inside every mutation - including
         * track deletion, which reaches mixes through the foreign key without going near the manager.
         *
         * Reports failure instead of throwing. mix_data is JSON, json::parse throws on anything
         * malformed, and callers here are mid-transaction on a real library: one unreadable row would
         * otherwise end the process with earlier work already committed and no report written. A row
         * that cannot be read is a row this code declines to draw conclusions from, not a reason to
         * abandon everything that came before it.
         *
         * @param stmt Positioned on a row selecting mixTrackColumnsForDecoding, in that order.
         * @param trackOut Written only on success.
         * @param errorOut Optional; receives why it failed. Worth asking for: this is a JSON parse
         *        error, so it never reaches SqliteDatabase::getLastError, and a caller reporting that
         *        instead would print an empty or stale reason in place of the real one.
         * @return False if mix_data could not be understood.
         */
        bool mixTrackFromStatement(const SqliteStatement &stmt, MixTrack &trackOut, std::string *errorOut = nullptr);

        /**
         * @brief The MixTracks columns mixTrackFromStatement reads, in the order it reads them.
         *
         * Selected by name rather than with a star, for the same reason as trackColumnsForDecoding: the
         * decoder reads the row by position, and the order a table declares its columns in is not
         * something a query should depend on. A migrated database carried an is_active column here for
         * a while, and the v32 rung rebuilds the table - either would have shifted every field along by
         * one under a SELECT *.
         *
         * This and mixTrackFromStatement are one contract. Change them together.
         */
        inline constexpr std::string_view mixTrackColumnsForDecoding{"mix_id, track_id, order_in_mix, mix_data"};

        /**
         * @brief Binds a MixTrack to an INSERT INTO MixTracks statement, mix_data included.
         * @return False if any parameter could not be bound.
         */
        bool bindMixTrackToStatement(SqliteStatement &stmt, const MixTrack &track);

    } // namespace database
} // namespace jucyaudio
