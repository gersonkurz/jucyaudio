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

namespace jucyaudio
{
    namespace database
    {
        /**
         * @brief What a mix's summary columns say, or should say.
         *
         * Both figures together, never one on its own: they describe the same set of rows, and a count
         * that belongs to a different moment from the length is worse than neither.
         */
        struct MixSummary final
        {
            int64_t trackCount{0};
            Duration_t totalLength{0};
        };

        /// @brief The outcome of rechecking one mix, both figures read inside the same transaction.
        struct MixDurationCheck final
        {
            /// @brief True only when the row was actually rewritten.
            bool changed{false};

            /// @brief What the row said before.
            MixSummary previous;

            /// @brief What it says now - equal to previous when nothing was changed.
            MixSummary current;
        };

    } // namespace database
} // namespace jucyaudio
