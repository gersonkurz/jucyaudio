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
         * | `#JAFORMAT:<n>`                     | which version of this layout wrote the file |
         * | `#EXTMIXINCOMPLETE:1`               | only when tracks are known to be missing |
         * | `# --- N track(s) missing here ---` | wherever the mix had tracks the record does not |
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
         * The file is written to a temporary and then put in place atomically, so a failure or an
         * interruption leaves the previous one intact. A half-written disaster-recovery file would be
         * worse than none, because it would be trusted.
         *
         * Under NeverReplace the name is claimed with a hard link rather than checked and then
         * written. Asking whether a file exists and writing if it does not is a race with a window in
         * between, and it answers for what a symlink points at rather than for the name itself - so a
         * dangling one reads as free and is then destroyed. Creating a link fails when the name is
         * taken, atomically, whatever is under it.
         *
         * @param targetPath Where the m3u should end up.
         * @param entries The committed recovery record, in order.
         * @param mode Whether an existing file at that path may be replaced.
         * @param targetExistedOut Optional. Set to true when NeverReplace found a usable file already
         *        at the name and wrote nothing - which is not a failure, and is reported as success
         *        with this set. A name blocked by something that is not a usable file is a failure
         *        instead, and comes back as an error with this left false.
         * @param totalDuration The mix's total length. Nothing when the record predates that being
         *        stored - the header line is then left out entirely rather than written as zero, and is
         *        never filled in from the live mix, which would describe a different mix from the tracks
         *        beneath it.
         * @return An empty string on success, otherwise why it failed.
         */
        /// @brief What is sitting at a playlist name.
        ///
        /// Not a judgement about content - nothing here reads the file, and none of these states ever
        /// authorises replacing anything. It separates the two reasons a name might be unavailable,
        /// which call for different reports: one is "there is already a playlist here, nothing to do",
        /// the other is "something is in the way and the playlist this mix needs cannot exist".
        enum class M3UTargetState
        {
            /// @brief Nothing of that name. It can be written.
            Free,

            /// @brief A regular file that could actually be opened for reading. Left alone; nothing to
            ///        do. Opened rather than merely stat-ed: permissions, or another process holding it
            ///        without sharing, make a file that exists and cannot be read.
            HoldsFile,

            /// @brief The name is taken by something that is not a usable file - a directory, or a
            ///        symlink pointing at nothing. Left alone too, but reported: no playlist can be
            ///        written here, so the mix has none however successful the run looks.
            Blocked
        };

        /**
         * @brief What occupies a playlist name, without reading or changing anything.
         *
         * Answers for the name rather than for what a symlink points at when deciding whether it is
         * taken, then follows the link, and finally opens the file to establish that it can be. A
         * dangling symlink is therefore taken but not usable, which is exactly the case an exists()
         * check gets wrong in both directions - and so is a file whose permissions deny it.
         *
         * A lookup that fails is reported as Blocked: not knowing is not the same as knowing the name
         * is free, and only the second is safe to act on.
         */
        M3UTargetState mixRecoveryM3UTargetState(const std::filesystem::path &path);

        /// @brief What to do about a file already sitting at the target.
        enum class M3UWriteMode
        {
            /// @brief Replace it. For the companion written beside an export: the audio has just
            ///        been rendered, and the playlist describing it has to describe this one.
            ReplaceExisting,

            /// @brief Leave it alone and report that it was there. For maintenance passes, which
            ///        have no business deciding that somebody else's file is obsolete.
            NeverReplace
        };

        std::string writeMixRecoveryM3U(const std::filesystem::path &targetPath,
            const std::vector<database::MixRecoveryEntry> &entries,
            std::optional<Duration_t> totalDuration,
            M3UWriteMode mode = M3UWriteMode::ReplaceExisting,
            bool *targetExistedOut = nullptr);

        /**
         * @brief The layout version written into every playlist this code produces.
         *
         * Written, never read. Nothing in the application decides anything from it, and that is
         * deliberate - a rule for deciding when a file of yours is out of date is a rule for deciding
         * when to overwrite it, and these files are meant to outlive the code that made them.
         *
         * It is here for the person opening one in ten years with no application to hand, who needs to
         * know which layout they are looking at. Version 1 had no marker and no missing-track
         * annotations; version 2 has both. A stamp like this cannot be added retroactively to files
         * already written, which is the whole reason to start now.
         */
        inline constexpr int kMixRecoveryM3UFormat = 2;


        /// @brief The companion m3u path for an exported audio file: same name, .m3u extension.
        std::filesystem::path companionM3UPathFor(const std::filesystem::path &audioPath);

    } // namespace audio
} // namespace jucyaudio
