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

#include <string>

namespace jucyaudio
{
    namespace database
    {
        /**
         * @brief One track's place in a mix, as it stood when that mix was exported.
         *
         * A record of something that happened, not a live view. Everything here is a copy taken at
         * capture time and is expected to disagree with the library eventually - that is the point. It
         * survives the tracks it describes being deleted, and is what remains to identify them by.
         */
        struct MixRecoveryEntry final
        {
            MixId mixId{0};
            int orderInMix{0};
            Timestamp_t capturedAt;
            std::string mixName;

            /**
             * @brief The track id this entry had when it was captured. Historical only.
             *
             * Never treat this as identity. `Tracks.track_id` is `INTEGER PRIMARY KEY` without
             * `AUTOINCREMENT`, so SQLite reuses the highest deleted rowid and this may since have been
             * handed to an entirely different track. Useful as a first guess to check, nothing more.
             */
            TrackId trackId{0};

            std::string artistName;
            std::string albumTitle;
            std::string title;

            /// @brief The file's name, and the folder it was in, as text.
            /// Text rather than a folder id, because a folder id means nothing once the folder row is
            /// gone - which is the situation this exists for.
            std::string filename;
            std::string folderPath;

            Duration_t duration{0};
            int64_t filesizeBytes{0};
            int64_t bpm{0};

            /**
             * @brief The mix's per-track settings, copied verbatim from MixTracks.mix_data.
             *
             * Deliberately opaque here. It is never deserialised and re-serialised on the way through,
             * because a round trip through a parser is a chance to drop a field the parser does not know
             * about, and this is what makes the record a backup of the mix rather than of a tracklist.
             */
            std::string mixData;
        };

    } // namespace database
} // namespace jucyaudio
