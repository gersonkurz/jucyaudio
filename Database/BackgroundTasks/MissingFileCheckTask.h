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

#include <Database/Includes/ILongRunningTask.h>
#include <Database/Includes/TrackInfo.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        namespace background_tasks
        {
            /**
             * @brief Re-checks whether specific tracks' files are on disk, and fixes is_missing.
             *
             * The library scan is the other way to correct this flag, but it walks entire roots. This
             * exists for the case where you already know which tracks you fixed and want to say so -
             * plugged a drive back in, moved a folder back - without a full rescan.
             *
             * Deliberately narrow: it answers "is the file where the database says it is" and nothing
             * more. It does not re-read tags, durations or anything else about the file, because writing
             * those back needs an update that touches only the columns a scan owns, and no such
             * operation exists yet - saveTrackInfo's UPDATE writes every column, so it would flatten
             * BPM, energy analysis, ratings and play history with defaults.
             *
             * A background task rather than an inline loop for the usual reason: a single exists()
             * against a disconnected network path can block for an OS-level timeout, and this is
             * precisely the situation where that is likely.
             */
            class MissingFileCheckTask final : public ILongRunningTask
            {
            public:
                /// @brief One track to check, with everything the worker needs already worked out.
                ///
                /// The path is resolved by the caller rather than in run(), because
                /// TrackInfo::reconstructFullPath() reads the library's folder cache, and that is not for
                /// a worker thread to touch. Everything here is a plain value for the same reason.
                struct WorkItem final
                {
                    TrackId trackId{0};
                    std::filesystem::path path;
                    std::string filename;  ///< For log messages; a bare id sends the reader to SQL.
                    bool wasMissing{false};
                };

                /// @brief Runs when the check finishes, whatever the reason - completed, cancelled, or
                /// the dialog dismissed underneath it. Called on the worker thread, so marshal to the
                /// message thread before touching anything that belongs to it.
                ///
                /// Needed because tracks are committed one at a time, so even an abandoned run can leave
                /// the database changed. TaskDialog discards its own completion callback when its window
                /// is closed with the title-bar button, which would leave those rows corrected in the
                /// database but stale on screen.
                using OnFinished = std::function<void()>;

                /// @param items The tracks to check. Build these on the message thread.
                /// @param onFinished Optional; see above.
                explicit MissingFileCheckTask(std::vector<WorkItem> items, OnFinished onFinished = nullptr);

                void run(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel) override;

                /// @brief Tracks that were flagged missing and whose file has come back.
                int getRecoveredCount() const
                {
                    return m_recoveredCount;
                }

                /// @brief Tracks that were not flagged and whose file is gone.
                int getNewlyMissingCount() const
                {
                    return m_newlyMissingCount;
                }

                /// @brief Tracks that were already flagged and whose file is still gone.
                int getStillMissingCount() const
                {
                    return m_stillMissingCount;
                }

            private:
                std::vector<WorkItem> m_items;
                OnFinished m_onFinished;
                int m_recoveredCount{0};
                int m_newlyMissingCount{0};
                int m_stillMissingCount{0};
            };

        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio
