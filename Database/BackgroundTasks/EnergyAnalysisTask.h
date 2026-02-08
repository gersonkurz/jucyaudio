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
#include <vector>

namespace jucyaudio::database::background_tasks
{
    /**
     * @brief Long-running task that analyzes energy data for tracks that don't have it cached.
     *
     * This task is run before creating a mix to ensure all tracks have energy analysis data.
     * It shows progress to the user and can be cancelled.
     */
    class EnergyAnalysisTask final : public database::ILongRunningTask
    {
    public:
        /**
         * @brief Construct a task to analyze energy for the given tracks.
         * @param tracks The tracks to potentially analyze (only those missing data will be processed)
         */
        explicit EnergyAnalysisTask(const std::vector<database::TrackInfo>& tracks);

        /**
         * @brief Run the analysis task.
         * @param progressCb Callback to report progress (0-100)
         * @param completionCb Callback when task completes
         * @param shouldCancel Atomic flag to check for cancellation
         */
        void run(database::ProgressCallback progressCb,
                 database::CompletionCallback completionCb,
                 std::atomic<bool>& shouldCancel) override;

        /**
         * @brief Get the number of tracks that needed analysis.
         */
        int getTracksAnalyzedCount() const { return m_tracksAnalyzedCount; }

        /**
         * @brief Check if the task completed successfully (not cancelled/failed).
         */
        bool wasSuccessful() const { return m_wasSuccessful; }

    private:
        std::vector<database::TrackInfo> m_tracks;
        int m_tracksAnalyzedCount = 0;
        bool m_wasSuccessful = false;
    };

} // namespace jucyaudio::database::background_tasks
