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
         * @brief Recomputes every mix's stored summary - track_count and total_length - once.
         *
         * Six of the eight paths that wrote a mix used to pass along whatever total they happened to be
         * holding rather than deriving one, so appending tracks stored the previous length instead of the
         * new one. Repeatedly. One five-hour mix in the library ended up recorded as sixty-six hours long.
         *
         * That is fixed at the point of writing, which corrects a mix the next time it is saved - but an
         * exported mix is never saved again, so without this pass those stay wrong for good. Hence a
         * one-off: it exists to be run once and then to keep reporting that there is nothing left to do.
         *
         * Both summary columns are rewritten, total_length and track_count, and both are reported. They
         * describe the same set of rows, so recomputing one and leaving the other would produce a pair
         * that was never true together - and a mix whose count alone is wrong would otherwise appear in
         * the report as an unexplained "5:30:00 -> 5:30:00".
         *
         * Nothing else in the row is touched, and only mixes whose stored figures disagree with the walk
         * are written at all. A mix that is already right is left completely alone, so its row keeps the
         * timestamp it had - which means running this a second time is also how you verify the first
         * run, rather than rewriting everything again and learning nothing.
         *
         * Like the recovery backfill this operates on the real library, so the caller is expected to have
         * taken a backup before opening it.
         *
         * @param configRoot The resolved config root. The report is written underneath it.
         * @return 0 if the run completed, 1 if it could not.
         */
        int runMixDurationRepair(const std::filesystem::path &configRoot);

    } // namespace tests
} // namespace jucyaudio
