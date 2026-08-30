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

#include <Database/Includes/Constants.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/MixSummary.h>

#include <vector>

namespace jucyaudio
{
    namespace database
    {
        class SqliteDatabase;
        class SqliteTransaction;

        /**
         * @brief Every row of one mix, in order, or nothing at all.
         *
         * Nothing is written to @p tracksOut unless the whole query succeeded. That is the point of it:
         * reading rows one, two and three and then failing on four must not hand back a three-track mix,
         * because callers save what they were given and a save rewrites the whole row set. A prefix
         * accepted as a mix is a mix with its tail deleted, permanently, at the next save.
         *
         * Reports why it failed - including malformed mix_data, which is a JSON parse error and never
         * reaches SqliteDatabase::getLastError().
         */
        DbResult readMixTracksChecked(SqliteDatabase &db, MixId mixId, std::vector<MixTrack> &tracksOut);

        /**
         * @brief Reads a mix and works out what Mixes.track_count and Mixes.total_length should be.
         *
         * @param db The connection; the caller is expected to be inside a transaction already.
         * @param mixId The mix to walk.
         * @param summaryOut Written only on success.
         * @return Success, or why the mix could not be read. An empty mix succeeds, reporting zero.
         */
        DbResult computeMixSummary(SqliteDatabase &db, MixId mixId, MixSummary &summaryOut);

        /**
         * @brief Brings a mix's summary columns back in step with its rows.
         *
         * Called by every operation that changes what is in a mix, from inside that operation's
         * transaction and after its own writes have landed, so the figures describe the rows that are
         * about to be committed.
         *
         * This exists because the length used to be whatever the caller passed in, and six of the eight
         * callers passed the mix's previous total - so appending tracks stored the length the mix had
         * before the append, repeatedly. One five-hour mix ended up recorded as sixty-six hours long.
         * Deriving it here means a deletion, a reorder, a cue edit or a cascade cannot leave the stored
         * figures behind.
         *
         * Lives apart from SqliteMixManager because deleting a track is a mix mutation too: the foreign
         * key on MixTracks.track_id cascades, so SqliteTrackDatabase changes mixes without going near
         * the mix manager, and it needs this as well.
         *
         * @param summaryOut Optional; receives the figures that were written.
         * @return Failure on any database or decoding error, so the caller rolls back rather than
         *         committing a mix whose stored length describes a different mix.
         */
        DbResult refreshMixSummary(SqliteDatabase &db, SqliteTransaction &transaction, MixId mixId, MixSummary *summaryOut = nullptr);

        /**
         * @brief Every mix that has a row for any of these tracks.
         *
         * For use before deleting tracks: afterwards the cascade has removed the rows and there is
         * nothing left to tell you which mixes were affected.
         */
        DbResult findMixesContainingTracks(SqliteDatabase &db, const std::vector<TrackId> &trackIds, std::vector<MixId> &mixIdsOut);

    } // namespace database
} // namespace jucyaudio
