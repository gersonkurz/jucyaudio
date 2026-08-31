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
#include <Database/Includes/MixRecoveryEntry.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace audio
    {
        /**
         * @brief Writes the human-readable companion to a mix's recovery record.
         *
         * This is the layer that survives losing the database entirely. Nothing reads it back - it is
         * meant to be opened in a text editor by someone who has just heard a track in one of their own
         * mixes and wants to know what it was, or who is rebuilding a library from nothing. That is why
         * it carries start times: you can find a track by where it sits in the mix.
         *
         * Rendered from an already-committed recovery record rather than from a fresh query, so the file
         * and the table cannot describe different states of the same mix.
         *
         * Format, fixed so a reader written later cannot guess wrong:
         *
         * | Line                                | Meaning                                  |
         * |-------------------------------------|------------------------------------------|
         * | `#EXTM3U`                           | standard header                          |
         * | `#EXTMIX:<name>`                    | mix name                                 |
         * | `#EXTMIXDURATION:<seconds>`         | total length, seconds; absent if unknown |
         * | `#EXTMIXINCOMPLETE:1`               | only when tracks are known to be missing |
         * | `#EXTINF:<seconds>,<artist> - <title>` | standard, seconds, rounded            |
         * | `#JAALBUM:<album>`                  | album                                    |
         * | `#JASTART:<milliseconds>`           | where this track starts within the mix; may be negative |
         * | `#JADURATION:<milliseconds>`        | exact track length                       |
         * | `#JASIZE:<bytes>`                   | file size                                |
         * | `#JATRACKID:<id>`                   | the track id at the time of capture      |
         * | *(bare line)*                       | absolute path                            |
         *
         * `#EXTINF` is seconds by definition and players expect that, but seconds are too coarse to
         * confirm a match when re-identifying a file that has moved - hence the exact millisecond
         * duration beside it. The `#JA` prefix keeps these out of the way of any `#EXT` tag a player
         * might try to interpret, while remaining comments that every player ignores.
         *
         * The file is written to a temporary and then atomically replaced, so a failure or an
         * interruption leaves the previous one intact. A half-written disaster-recovery file would be
         * worse than none, because it would be trusted.
         *
         * @param targetPath Where the m3u should end up.
         * @param entries The committed recovery record, in order.
         * @param totalDuration The mix's total length. Nothing when the record predates that being
         *        stored - the header line is then left out entirely rather than written as zero, and is
         *        never filled in from the live mix, which would describe a different mix from the tracks
         *        beneath it.
         * @return An empty string on success, otherwise why it failed.
         */
        std::string writeMixRecoveryM3U(const std::filesystem::path &targetPath,
            const std::vector<database::MixRecoveryEntry> &entries,
            std::optional<Duration_t> totalDuration);

        /// @brief The companion m3u path for an exported audio file: same name, .m3u extension.
        std::filesystem::path companionM3UPathFor(const std::filesystem::path &audioPath);

    } // namespace audio
} // namespace jucyaudio
