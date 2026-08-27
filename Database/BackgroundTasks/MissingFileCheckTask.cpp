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

#include <Database/BackgroundTasks/MissingFileCheckTask.h>

#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>

#include <spdlog/spdlog.h>

#include <format>

namespace jucyaudio
{
    namespace database
    {
        namespace background_tasks
        {
            MissingFileCheckTask::MissingFileCheckTask(std::vector<WorkItem> items, OnFinished onFinished)
                : ILongRunningTask{"Checking Files", true}, // cancellable
                  m_items{std::move(items)},
                  m_onFinished{std::move(onFinished)}
            {
                spdlog::info("[MissingFileCheck] Created for {} track(s)", m_items.size());
            }

            void MissingFileCheckTask::run(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel)
            {
                // Fires on every way out of this function, including the early returns below. Tracks are
                // committed one at a time, so an abandoned run still leaves the database changed, and
                // whoever is showing those rows needs to hear about it even when the dialog that started
                // this is already gone.
                struct FinishedNotifier final
                {
                    const OnFinished &callback;
                    ~FinishedNotifier()
                    {
                        if (callback)
                        {
                            callback();
                        }
                    }
                } notifier{m_onFinished};

                if (m_items.empty())
                {
                    completionCb(true, "No tracks to check.");
                    return;
                }

                auto &db = theTrackLibrary.getTrackDatabase();

                for (size_t i = 0; i < m_items.size(); ++i)
                {
                    if (shouldCancel.load())
                    {
                        // Whatever was corrected before the cancel stays corrected: each track is its own
                        // independent update, so stopping half way leaves the database consistent, just
                        // less complete than asked for.
                        completionCb(false, std::format("Cancelled after {} of {} track(s).", i, m_items.size()));
                        return;
                    }

                    const auto &item = m_items[i];

                    if (progressCb)
                    {
                        // Named rather than counted: if a lookup hangs, the user needs to see on what.
                        // pathToString, not path::string(): the narrow form of a Windows path is not
                        // UTF-8 and throws on characters the active code page cannot represent.
                        progressCb(static_cast<int>((i * 100) / m_items.size()), std::format("Checking {}", pathToString(item.path.filename())));
                    }

                    std::error_code ec;
                    const bool onDisk = std::filesystem::exists(item.path, ec);

                    if (onDisk == !item.wasMissing)
                    {
                        // Already says what the disk says. Nothing to write - and on a selection of a few
                        // thousand tracks, not writing is most of the work avoided.
                        m_stillMissingCount += item.wasMissing ? 1 : 0;
                        continue;
                    }

                    const auto result = db.setTrackPathMissing(item.trackId, !onDisk);
                    if (!result.isOk())
                    {
                        spdlog::error("[MissingFileCheck] Failed to update track {} ({}): {}", item.trackId, item.filename, result.errorMessage);
                        completionCb(false, std::format("Could not update track {}: {}", item.trackId, result.errorMessage));
                        return;
                    }

                    if (onDisk)
                    {
                        ++m_recoveredCount;
                        spdlog::info("[MissingFileCheck] Track {} ({}) is back on disk; cleared its missing flag.", item.trackId, item.filename);
                    }
                    else
                    {
                        ++m_newlyMissingCount;
                        spdlog::warn("[MissingFileCheck] Track {} ({}) is gone: {}", item.trackId, item.filename, pathToString(item.path));
                    }
                }

                const auto summary = std::format("Checked {} track(s): {} recovered, {} newly missing, {} still missing.",
                    m_items.size(),
                    m_recoveredCount,
                    m_newlyMissingCount,
                    m_stillMissingCount);
                spdlog::info("[MissingFileCheck] {}", summary);
                completionCb(true, summary);
            }

        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio
