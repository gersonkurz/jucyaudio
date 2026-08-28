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

#include <filesystem>

namespace jucyaudio
{
    namespace tests
    {
        /**
         * @brief Records every mix that has never been recorded, in one pass.
         *
         * Mixes made before recovery data existed carry none, and the tracks they name are being lost
         * quietly as the library changes underneath them. This captures all of them while the rows are
         * still there to capture, and writes each one a companion playlist. After it, every existing mix
         * is protected and every new one is protected at export.
         *
         * A mix that is not intact is skipped and named, never captured - the same rule that governs an
         * ordinary capture, for the same reason. Those are precisely the mixes whose records would be
         * worth having, so writing a damaged one over nothing would be the last chance to notice.
         *
         * Skips are not failures. The exit code reports whether the run itself worked, not whether every
         * mix was in a fit state to record.
         *
         * Unlike the self tests, this deliberately operates on the real library - that is the entire
         * point - so it takes a confirmed backup first and refuses to start without one. Opening the
         * database can migrate it, which is not something to do to a multi-gigabyte library on the
         * strength of a backup that merely reports success.
         *
         * @param configRoot The resolved config root. Playlists are written to MixBackups underneath it.
         * @return 0 if the run completed, 1 if it could not. Skipped mixes do not make it fail.
         */
        int runMixRecoveryBackfill(const std::filesystem::path &configRoot);

    } // namespace tests
} // namespace jucyaudio
