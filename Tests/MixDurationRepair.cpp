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

#include <Tests/MixDurationRepair.h>

#include <Database/Includes/MixInfo.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>

#include <spdlog/spdlog.h>

#include <format>
#include <fstream>
#include <locale>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace tests
    {
        using namespace database;

        int runMixDurationRepair(const std::filesystem::path &configRoot)
        {
            const auto resultsPath = configRoot / "mix-duration-repair.txt";

            auto &mixManager = theTrackLibrary.getMixManager();

            // getAllMixes, not getMixes: the latter returns only mixes with no export folder, which in a
            // library where everything has been exported is none of them. The mixes that most need this
            // are exactly the exported ones, because they are never saved again and so are never
            // corrected by the fix at the point of writing.
            std::vector<MixInfo> mixes;
            if (const auto listResult = mixManager.getAllMixes(mixes); !listResult.isOk())
            {
                spdlog::error("[DurationRepair] Could not enumerate the mixes: {}", listResult.errorMessage);
                return 1;
            }

            spdlog::info("[DurationRepair] {} mixes to check.", mixes.size());

            int repaired = 0;
            int leftAlone = 0;
            int failed = 0;
            std::vector<std::string> repairedLines;
            std::vector<std::string> failedLines;

            for (const auto &mix : mixes)
            {
                // One call, one transaction. Reading the rows, deciding whether they disagree with the
                // stored figures, and writing all happen inside it. Deciding out here and writing
                // afterwards would leave a gap in which another instance of the application could edit
                // the mix - and this would then store a length describing a mix that no longer exists in
                // that form.
                MixDurationCheck check;
                if (const auto result = mixManager.recomputeMixDuration(mix.mixId, check); !result.isOk())
                {
                    ++failed;
                    failedLines.push_back(std::format("mix {} ({}): {}", mix.mixId, mix.name, result.errorMessage));
                    spdlog::error("[DurationRepair] Mix {} failed: {}", mix.mixId, result.errorMessage);
                    continue;
                }

                if (!check.changed)
                {
                    // Either already correct or empty. Untouched either way, which is what makes a second
                    // run a check of the first rather than a repeat of it.
                    ++leftAlone;
                    continue;
                }

                ++repaired;

                // Count as well as length, because the repair rewrites both. Reporting only the length
                // would show a mix whose count alone was wrong as "5:30:00 -> 5:30:00" and lose what
                // the count used to be - in the one file that is the whole audit record.
                //
                // All four figures come from inside the repair transaction, so they describe one
                // moment. Quoting the listing read before the run, or reading the row again afterwards,
                // would let a concurrent edit put numbers in this report that were never true together.
                repairedLines.push_back(std::format("mix {} ({}): {} tracks, {} -> {} tracks, {}",
                    mix.mixId,
                    mix.name,
                    check.previous.trackCount,
                    durationToString(check.previous.totalLength),
                    check.current.trackCount,
                    durationToString(check.current.totalLength)));

                if (repaired % 100 == 0)
                {
                    spdlog::info("[DurationRepair] {} mixes corrected so far...", repaired);
                }
            }

            const auto summary =
                std::format("{} mixes: {} corrected, {} left alone, {} failed.", mixes.size(), repaired, leftAlone, failed);
            spdlog::info("[DurationRepair] {}", summary);

            {
                std::ofstream out{resultsPath, std::ios::trunc};
                if (out)
                {
                    // The application sets a global locale with thousands separators and operator<<
                    // honours it. Nothing here is read by machine, but a report claiming a library holds
                    // "1,109" mixes reads worse than one that does not pretend.
                    out.imbue(std::locale::classic());

                    out << "jucyaudio mix duration repair\n";
                    out << (failed == 0 ? "RESULT: OK\n" : "RESULT: FAILED\n");
                    out << summary << "\n";

                    if (!repairedLines.empty())
                    {
                        out << "\nCorrected:\n";
                        for (const auto &line : repairedLines)
                        {
                            out << "  " << line << "\n";
                        }
                    }

                    if (!failedLines.empty())
                    {
                        out << "\nFailed:\n";
                        for (const auto &line : failedLines)
                        {
                            out << "  " << line << "\n";
                        }
                    }

                    // Closed and then inspected rather than trusting that opening was the only thing that
                    // could fail: a disk that fills part way through leaves a truncated report and an
                    // otherwise happy stream.
                    out.flush();
                    out.close();
                    if (!out)
                    {
                        ++failed;
                        spdlog::error("[DurationRepair] The results file {} was not written completely.", pathToString(resultsPath));
                    }
                }
                else
                {
                    ++failed;
                    spdlog::error("[DurationRepair] Could not write results to {}", pathToString(resultsPath));
                }
            }

            return failed == 0 ? 0 : 1;
        }

    } // namespace tests
} // namespace jucyaudio
