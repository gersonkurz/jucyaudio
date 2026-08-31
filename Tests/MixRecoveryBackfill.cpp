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

#include <Tests/MixRecoveryBackfill.h>

#include <Audio/MixRecoveryM3U.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/MixRecoveryEntry.h>
#include <Database/Includes/TrackQueryArgs.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace tests
    {
        using namespace database;

        namespace
        {
            /// @brief Turn a mix name into something a filesystem will accept, keeping the id.
            ///
            /// The id leads, because it is the only part guaranteed unique and the only part that lets a
            /// file be traced back to a row. Mixes.name is unique but case-insensitively so, and may hold
            /// characters no filesystem takes - two mixes differing only in case would otherwise collide
            /// on Windows.
            std::string backupFilename(MixId mixId, const std::string &mixName)
            {
                // Byte-wise over UTF-8 is deliberate: anything below 0x20 or in the reserved set goes,
                // everything else - including the multi-byte sequences this library is full of - is left
                // exactly as it is. Filesystems take those; it is the ASCII punctuation they object to.
                static constexpr std::string_view reserved{"<>:\"/\\|?*"};

                std::string safe;
                safe.reserve(mixName.size());
                for (const unsigned char c : mixName)
                {
                    safe.push_back((c < 0x20 || reserved.find(static_cast<char>(c)) != std::string_view::npos) ? '_' : static_cast<char>(c));
                }

                // Trailing dots and spaces are legal to create on POSIX and quietly stripped by Windows,
                // which would make a name that no longer matches what was written.
                while (!safe.empty() && (safe.back() == '.' || safe.back() == ' '))
                {
                    safe.pop_back();
                }

                return std::format("{:05d} - {}.m3u", mixId, safe);
            }
        } // namespace

        int runMixRecoveryBackfill(const std::filesystem::path &configRoot, RecoveryCaptureMode mode)
        {
            const auto outputDir = configRoot / "MixBackups";
            const auto resultsPath = configRoot / "backfill-results.txt";

            std::error_code ec;
            std::filesystem::create_directories(outputDir, ec);
            if (ec)
            {
                spdlog::error("[Backfill] Could not create {}: {}", pathToString(outputDir), ec.message());
                return 1;
            }

            auto &mixManager = theTrackLibrary.getMixManager();

            // getAllMixes, emphatically not getMixes: that one returns only mixes with no export folder,
            // and may hide offline ones depending on a UI setting. In a library where every mix has been
            // exported it returns nothing, and this would have reported a clean, successful run over zero
            // mixes - the most convincing possible way to do nothing.
            std::vector<MixInfo> mixes;
            if (const auto listResult = mixManager.getAllMixes(mixes); !listResult.isOk())
            {
                // Not something to work around: acting on part of the library while reporting success is
                // the failure this command exists to prevent, one level up.
                spdlog::error("[Backfill] Could not enumerate the mixes: {}", listResult.errorMessage);
                return 1;
            }

            spdlog::info("[Backfill] {} mixes to consider.", mixes.size());

            if (mixes.empty())
            {
                // Never right: a library with no mixes at all has nothing to back up, but reaching here
                // after asking for every one of them is much more likely to mean the query is wrong.
                spdlog::warn("[Backfill] No mixes found. Nothing to do.");
            }

            int captured = 0;
            int skipped = 0;
            int failed = 0;
            int64_t rowsWritten = 0;
            std::vector<std::string> skippedLines;
            std::vector<std::string> failedLines;

            int alreadyRecorded = 0;
            int playlistsRepaired = 0;
            int partial = 0;

            for (const auto &mix : mixes)
            {
                // Mixes that already have a record keep it, untouched - which is what "every mix that
                // has none" means and what this did not previously do. Only the playlist beside it may
                // be written, and only when there is no file there at all.
                //
                // The danger is specific. An export-time record was checked against the audio that was
                // actually rendered; this command has no rendered baseline to check against, because
                // nothing is being rendered. So re-recording a mix that has since been unlocked and
                // edited would replace an accurate description of the mp3 with a description of the
                // current editable state - and the mp3 is the thing someone will be holding when they
                // need the record. Running this twice must not be able to do that.
                std::vector<MixRecoveryEntry> existing;
                const auto readResult = mixManager.getRecoveryData(mix.mixId, existing);
                if (!readResult.isOk())
                {
                    // Could not tell whether a record exists, so cannot safely write one over it.
                    ++failed;
                    failedLines.push_back(std::format("mix {} ({}): could not read existing recovery data: {}", mix.mixId, mix.name, readResult.errorMessage));
                    spdlog::error("[Backfill] Mix {}: {}", mix.mixId, readResult.errorMessage);
                    continue;
                }

                if (!existing.empty())
                {
                    ++alreadyRecorded;

                    // Recorded, but is the playlist actually there? The rows are committed before the
                    // playlist is written, so a run that failed at that point leaves a mix that looks
                    // done and is not. Without this, the next run would skip it as already recorded and
                    // then overwrite the report that said it failed - the failure would disappear while
                    // the missing file stayed missing.
                    //
                    // Written from the existing record, never a fresh capture: the record was verified
                    // against the audio when it was made, and nothing here has a baseline to re-verify
                    // it against.
                    // Everything about this file comes from the record, including the name it is filed
                    // under. Using the mix's current name would look for the wrong file after a rename,
                    // find nothing, write a duplicate, and give it a filename disagreeing with the
                    // #EXTMIX line inside it.
                    const auto playlistPath = outputDir / pathFromString(backupFilename(mix.mixId, existing.front().mixName));

                    // NeverReplace, and the writer decides - not an exists() check followed by a write.
                    // An existing playlist is never replaced, whatever it contains: these are disaster
                    // recovery notes, and someone piecing a mix back together may well have written in
                    // the margins of one. Regenerating over that would destroy the very work the file
                    // exists to support.
                    //
                    // Which means a playlist written before the missing-track annotations existed keeps
                    // its old contents. Upgrading it is a deliberate act: move the file aside, or delete
                    // it, and run this again. That is a worse workflow than doing it automatically and a
                    // much better one than a rule that decides on its own when your file is stale.
                    bool playlistExisted = false;
                    if (const auto m3uError = audio::writeMixRecoveryM3U(playlistPath,
                            existing,
                            existing.front().mixTotalDuration,
                            audio::M3UWriteMode::NeverReplace,
                            &playlistExisted);
                        !m3uError.empty())
                    {
                        ++failed;
                        failedLines.push_back(std::format("mix {} ({}): recorded already, but publishing its playlist did not finish cleanly: {}",
                            mix.mixId,
                            mix.name,
                            m3uError));
                        spdlog::error("[Backfill] Mix {}: {}", mix.mixId, m3uError);
                    }
                    else if (!playlistExisted)
                    {
                        ++playlistsRepaired;
                        spdlog::info("[Backfill] Mix {} was already recorded; wrote its missing playlist.", mix.mixId);
                    }
                    continue;
                }

                // Is the name free, before anything is committed?
                //
                // The rows go in first and the playlist is written after, so a name already taken by
                // something unrelated would leave the mix recorded with a stranger's file beside it.
                // That is reported once - and then never again, because the next run sees a record and
                // takes the other branch, where an occupied name is the normal case. The mismatch would
                // sit there permanently under a RESULT: OK.
                //
                // So it is refused here instead. Nothing is committed, the mix stays unrecorded, and
                // every subsequent run reports it again until someone moves the file.
                //
                // The record has not been made yet, so the filename comes from the mix's current name;
                // for a capture happening now, that is exactly what the record will carry.
                const auto prospectivePath = outputDir / pathFromString(backupFilename(mix.mixId, mix.name));

                // Not exists(): that follows symlinks, so a dangling one reads as free, the record
                // commits, and the write then finds the name taken after all. Free means free.
                if (const auto occupant = audio::mixRecoveryM3UTargetState(prospectivePath); occupant != audio::M3UTargetState::Free)
                {
                    ++failed;
                    failedLines.push_back(occupant == audio::M3UTargetState::HoldsFile
                            ? std::format("mix {} ({}): a file already occupies {}, so it was not recorded",
                                  mix.mixId,
                                  mix.name,
                                  pathToString(prospectivePath))
                            : std::format("mix {} ({}): {} is blocked by something that is not a usable file, so it was not recorded",
                                  mix.mixId,
                                  mix.name,
                                  pathToString(prospectivePath)));
                    spdlog::error("[Backfill] Mix {} not recorded: {} is not free.", mix.mixId, pathToString(prospectivePath));
                    continue;
                }

                // No rendered snapshot to compare against - nothing was rendered. The completeness rule
                // still applies, which is what matters here.
                MixRecoveryCapture capture;
                const auto captureResult = mixManager.captureRecoveryData(mix.mixId, capture, nullptr, mode);

                if (!captureResult.isOk())
                {
                    ++failed;
                    failedLines.push_back(std::format("mix {} ({}): {}", mix.mixId, mix.name, captureResult.errorMessage));
                    spdlog::error("[Backfill] Mix {} failed: {}", mix.mixId, captureResult.errorMessage);
                    continue;
                }

                if (!capture.captured)
                {
                    ++skipped;
                    skippedLines.push_back(std::format("mix {} ({}): {}", mix.mixId, mix.name, capture.skipReason));
                    continue;
                }

                ++captured;
                partial += capture.incomplete ? 1 : 0;
                rowsWritten += static_cast<int64_t>(capture.entries.size());

                // Written from the rows just committed, like the companion beside an exported mix. Same
                // writer, same format, so a playlist from here and one from an export cannot disagree.
                // Named from the record just committed, not from the MixInfo this loop is walking:
                // those agree right now, and using the record keeps the filename and the #EXTMIX line
                // inside the file from ever drifting apart.
                //
                // pathFromString, not the raw std::string: appending UTF-8 to a path sends it through
                // the active code page on Windows, undoing the very preservation backupFilename does.
                const auto playlistPath = outputDir / pathFromString(backupFilename(mix.mixId, capture.entries.front().mixName));

                // NeverReplace here too. A mix having no record does not mean the folder has no file:
                // a stale one under the same name, or something put there by hand, would otherwise be
                // destroyed by a capture that had nothing to do with it.
                bool capturedPlaylistExisted = false;
                if (const auto m3uError = audio::writeMixRecoveryM3U(playlistPath,
                        capture.entries,
                        capture.totalDuration,
                        audio::M3UWriteMode::NeverReplace,
                        &capturedPlaylistExisted);
                    !m3uError.empty())
                {
                    // The record is committed and safe; only its readable twin is missing. Counted as a
                    // failure because for this command the artefacts are the whole deliverable.
                    ++failed;
                    // Neutral about which half went wrong, because the writer reports both kinds: no
                    // file at all, and a file that was published but whose temporary is still beside
                    // it. Saying "no playlist" about the second would send someone looking for a
                    // missing file that is sitting right there.
                    failedLines.push_back(
                        std::format("mix {} ({}): recorded, but publishing its playlist did not finish cleanly: {}", mix.mixId, mix.name, m3uError));
                    spdlog::error("[Backfill] Mix {} recorded but its playlist did not publish cleanly: {}", mix.mixId, m3uError);
                }
                else if (capturedPlaylistExisted)
                {
                    // The name was free a moment ago and is not now, so another instance took it while
                    // this one was capturing. Rare, and still reported: the mix is recorded with a file
                    // beside it that describes something else, which is worse than no file because it
                    // will be read. The check above is what stops this being the ordinary case.
                    ++failed;
                    failedLines.push_back(std::format("mix {} ({}): recorded, but a file is already at {} and was left alone",
                        mix.mixId,
                        mix.name,
                        pathToString(playlistPath)));
                    spdlog::error("[Backfill] Mix {} recorded but something already occupies {}", mix.mixId, pathToString(playlistPath));
                }

                if (captured % 100 == 0)
                {
                    spdlog::info("[Backfill] {} mixes recorded so far...", captured);
                }
            }

            // Any temporary still lying about, from this run or an earlier one.
            //
            // A failed unlink is reported by the writer when it happens and then never again: the next
            // run finds the playlist in place and says so. But the leftover is a second name for that
            // published playlist, and the next thing to open it for writing truncates the file it
            // points at. That is not a one-off notice, it is a standing hazard, so it is looked for
            // every time until it is gone.
            {
                // Advanced by hand, because a range-for calls the throwing operator++ and only the
                // constructor was given an error code. An enumeration that failed part way would have
                // left this command by exception, before it wrote the report saying what it had done.
                std::error_code listEc;
                std::filesystem::directory_iterator entry{outputDir, listEc};
                const std::filesystem::directory_iterator end;

                while (!listEc && entry != end)
                {
                    if (entry->path().extension() == ".m3utmp")
                    {
                        ++failed;
                        failedLines.push_back(std::format("a leftover temporary is still there and is a second name for a playlist beside it: {}",
                            pathToString(entry->path())));
                        spdlog::error("[Backfill] Leftover temporary: {}", pathToString(entry->path()));
                    }

                    entry.increment(listEc);
                }

                if (listEc)
                {
                    ++failed;
                    failedLines.push_back(std::format("could not check {} for leftover temporaries: {}", pathToString(outputDir), listEc.message()));
                }
            }

            const auto summary =
                std::format("{} mixes: {} recorded ({} of them partial, {} rows), {} already had a record ({} playlists written), "
                            "{} skipped, {} failed.",
                    mixes.size(),
                    captured,
                    partial,
                    rowsWritten,
                    alreadyRecorded,
                    playlistsRepaired,
                    skipped,
                    failed);
            spdlog::info("[Backfill] {}", summary);

            {
                std::ofstream out{resultsPath, std::ios::trunc};
                if (out)
                {
                    out << "jucyaudio mix recovery backfill\n";
                    out << (failed == 0 ? "RESULT: OK\n" : "RESULT: FAILED\n");
                    out << summary << "\n";
                    out << "\nPlaylists: " << pathToString(outputDir) << "\n";

                    // Every skipped mix is named, not just counted. These are the ones whose records
                    // would have been worth having, so a bare number would be the least useful possible
                    // way to report them.
                    if (!skippedLines.empty())
                    {
                        out << "\nSkipped - not intact, left exactly as they were:\n";
                        for (const auto &line : skippedLines)
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
                    // Closed explicitly and then inspected, rather than trusting that opening was the
                    // only thing that could go wrong. A disk that fills up part way through leaves a
                    // truncated report and an otherwise happy stream, and the flush that would have
                    // revealed it happens in the destructor where nobody looks.
                    out.flush();
                    out.close();
                    if (!out)
                    {
                        ++failed;
                        spdlog::error("[Backfill] The results file {} was not written completely.", pathToString(resultsPath));
                    }
                }
                else
                {
                    // Counted as a failure, not just logged. The recipe prints this file and then says
                    // whether the run worked; without it, a zero exit code would have the recipe announce
                    // success over a report nobody can read.
                    ++failed;
                    spdlog::error("[Backfill] Could not write results to {}", pathToString(resultsPath));
                }
            }

            // Skips are not failures. Under the default mode a mix that has already lost rows cannot be
            // recorded honestly, and saying so is this command working rather than breaking.
            return failed == 0 ? 0 : 1;
        }

    } // namespace tests
} // namespace jucyaudio
