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

#include <Audio/MixRecoveryM3U.h>

#include <Database/Includes/MixInfo.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <locale>
#include <optional>
#include <random>
#include <string>
#include <string_view>

namespace jucyaudio
{
    namespace audio
    {
        using namespace database;
        using json = nlohmann::json;

        namespace
        {
            /// @brief Where each track's audible content begins within the finished mix.
            ///
            /// The same ATTACH walk the audio export and the playlist export both use: each track starts
            /// where the previous one's attach-out meets this one's attach-in, and its audible content
            /// begins cueStart later. Recomputed here from the recovery record rather than shared with
            /// those, because those work from live MixTracks and this deliberately does not.
            ///
            /// @return One optional per entry. Nothing means the position is not known, which happens
            ///         from the first unreadable mix_data onwards.
            ///
            /// Why it poisons everything after it: each start is derived from the one before, so a
            /// default-constructed MixTrack standing in for an unreadable one does not merely lose its
            /// own position - it becomes the previous track for the next iteration, and every start from
            /// there on is computed from a value that was invented. A missing #JASTART is honest; a
            /// plausible wrong one sends someone looking at the wrong minute of a two-hour mix.
            std::vector<std::optional<Duration_t>> audibleStartTimes(const std::vector<MixRecoveryEntry> &entries)
            {
                std::vector<std::optional<Duration_t>> starts(entries.size());
                Duration_t previousFileStart{0};
                MixTrack previous{};
                bool positionsStillKnown = true;

                for (size_t i = 0; i < entries.size(); ++i)
                {
                    MixTrack current{};
                    if (!entries[i].mixData.empty())
                    {
                        try
                        {
                            json::parse(entries[i].mixData).get_to(current);
                        }
                        catch (const std::exception &e)
                        {
                            spdlog::warn("[MixRecoveryM3U] Could not parse mix_data for track {}: {}; positions from here on are unknown",
                                entries[i].trackId,
                                e.what());
                            positionsStillKnown = false;
                        }
                    }

                    if (!positionsStillKnown)
                    {
                        continue;
                    }

                    Duration_t fileStart{0};
                    if (i > 0)
                    {
                        fileStart = previousFileStart + previous.attachTo - current.attachFrom;
                    }

                    starts[i] = fileStart + current.cueStart;
                    previousFileStart = fileStart;
                    previous = current;
                }

                return starts;
            }

            /// @brief "Artist - Title", falling back as fields run out.
            std::string describe(const MixRecoveryEntry &entry)
            {
                if (!entry.artistName.empty() && !entry.title.empty())
                {
                    return std::format("{} - {}", entry.artistName, entry.title);
                }
                if (!entry.title.empty())
                {
                    return entry.title;
                }
                return entry.filename;
            }
        } // namespace

        M3UTargetState mixRecoveryM3UTargetState(const std::filesystem::path &path)
        {
            // symlink_status first, because it answers for the name itself. exists() follows the
            // link, so a dangling one reads as nothing there - and the name is very much taken.
            //
            // The returned type is what decides, not the error code. An absent file is reported through
            // both - not_found in the status, and on this platform an errno in the code as well - so
            // treating any error as trouble classifies every free name as blocked, which is how this
            // first shipped and why nothing could be written at all.
            std::error_code nameEc;
            const auto nameStatus = std::filesystem::symlink_status(path, nameEc);
            if (nameStatus.type() == std::filesystem::file_type::not_found)
            {
                return M3UTargetState::Free;
            }

            if (nameEc || nameStatus.type() == std::filesystem::file_type::none)
            {
                // Something is there, or the question could not be answered. Either way it is not free,
                // and not knowing is never a licence to write.
                return M3UTargetState::Blocked;
            }

            // Taken. Following the link now decides whether what is there is a file - a symlink to a
            // real playlist counts, a symlink to nothing does not, and neither does a directory that
            // happens to have the name. A not_found here means the link points at nothing, which is
            // blocked rather than free: the name is still occupied by the link itself.
            std::error_code targetEc;
            const auto targetStatus = std::filesystem::status(path, targetEc);
            if (!std::filesystem::is_regular_file(targetStatus))
            {
                return M3UTargetState::Blocked;
            }

            // And then it is opened, because being a regular file is not the same as being a readable
            // one. Permissions can deny it, and on Windows another process can hold it with sharing
            // that excludes readers. Reporting "there is already a playlist here" about a file nobody
            // can open would leave the mix with no usable record and the run saying it was fine.
            //
            // Opened and closed, nothing read: the question is whether it can be, not what it says.
            std::ifstream probe{path, std::ios::binary};
            if (!probe)
            {
                return M3UTargetState::Blocked;
            }

            return M3UTargetState::HoldsFile;
        }

        std::filesystem::path companionM3UPathFor(const std::filesystem::path &audioPath)
        {
            auto companion = audioPath;
            companion.replace_extension(".m3u");
            return companion;
        }

        std::string writeMixRecoveryM3U(const std::filesystem::path &targetPath,
            const std::vector<database::MixRecoveryEntry> &entries,
            std::optional<Duration_t> totalDuration,
            M3UWriteMode mode,
            bool *targetExistedOut)
        {
            if (targetExistedOut != nullptr)
            {
                *targetExistedOut = false;
            }

            if (entries.empty())
            {
                return "there is nothing to write - the recovery record is empty";
            }

            if (mode == M3UWriteMode::NeverReplace)
            {
                // A look before doing any work, and nothing rests on it.
                //
                // In the steady state every playlist is already there, so without this the maintenance
                // pass renders a full temporary for each one and deletes it again on discovering it had
                // nothing to do - and can fail for want of space, or on a temporary name too long for
                // the filesystem, while genuinely having nothing to do.
                //
                // Blocked and occupied are different answers. A directory or a dangling symlink under
                // this name means no playlist can be written for the mix at all, which a run must not
                // report as "already there, nothing to do".
                const auto occupant = mixRecoveryM3UTargetState(targetPath);
                if (occupant == M3UTargetState::HoldsFile)
                {
                    if (targetExistedOut != nullptr)
                    {
                        *targetExistedOut = true;
                    }
                    spdlog::info("[MixRecoveryM3U] {} is already there; left as it was.", pathToString(targetPath));
                    return {};
                }

                if (occupant == M3UTargetState::Blocked)
                {
                    return std::format("{} is taken by something that is not a usable file, so no playlist could be written there",
                        pathToString(targetPath));
                }
            }

            // Alongside the target, not in a system temp directory: a replace across volumes is not
            // atomic, and the destination directory is the one place guaranteed to be on the same one.
            //
            // The name is unique per attempt, because the application permits multiple instances and
            // two of them backfilling at once would otherwise pick the same one. Sharing it is worse
            // than it sounds: one instance can truncate the temporary while the other is linking it
            // into place, publishing half a file - and after the link the temporary is a second name
            // for the published file, so anything reopening it truncates what was just published.
            //
            // A clock reading plus a random token rather than a process id: there is no portable way
            // to ask for one, and this is at least as unlikely to collide without platform branches.
            // The same reasoning, and the same shape of name, as DatabaseBackupManager uses.
            const auto uniqueSuffix = std::format("{}-{:08x}",
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count(),
                std::random_device{}());
            auto tempPath = targetPath;
            tempPath += std::filesystem::path{std::format(".{}.m3utmp", uniqueSuffix)};

            // Every failure below returns early, and each one would otherwise leave the temporary behind
            // next to the real file, where it looks like a half-written recovery artefact. Disarmed only
            // once the replace has succeeded and the temporary is gone by definition.
            struct TempFileGuard final
            {
                const std::filesystem::path &path;
                bool armed{true};

                ~TempFileGuard()
                {
                    if (armed)
                    {
                        std::error_code ec;
                        std::filesystem::remove(path, ec);
                    }
                }
            } tempGuard{tempPath};

            const auto starts = audibleStartTimes(entries);

            {
                // Binary, so the newlines written are exactly the newlines that land in the file. A text
                // stream on Windows would turn each into a CRLF, which is harmless for players but means
                // the file is not the bytes this code thinks it wrote.
                std::ofstream out{tempPath, std::ios::out | std::ios::binary | std::ios::trunc};
                if (!out)
                {
                    return std::format("could not open {} for writing", pathToString(tempPath));
                }

                // The application sets a global locale with thousands separators, and operator<< honours
                // it - so a streamed 20757 came out as "20,757" and any reader parsing the number would
                // have got 20. This is a machine-readable file: numbers are digits, always. std::format
                // is locale-independent by default and was never affected, which is why only the one
                // streamed value went wrong.
                out.imbue(std::locale::classic());

                out << "#EXTM3U\n";
                out << std::format("#JAFORMAT:{}\n", kMixRecoveryM3UFormat);
                out << "#EXTMIX:" << entries.front().mixName << "\n";
                // Left out entirely when the record does not know it. A record written before the length
                // was stored has no answer, and printing nought would be an answer - a reader would take
                // a two-hour mix for an empty one.
                if (totalDuration.has_value())
                {
                    out << "#EXTMIXDURATION:" << std::chrono::duration_cast<std::chrono::seconds>(*totalDuration).count() << "\n";
                }

                // Said plainly, and before the tracks. This file exists to be read by someone piecing
                // a mix back together, and a partial tracklist that does not admit to being partial is
                // worse than none - they would take it for the whole thing and stop looking.
                if (!entries.front().isComplete)
                {
                    out << "#EXTMIXINCOMPLETE:1\n";
                    out << "# WARNING: this mix had already lost tracks when this record was made.\n";
                    out << "# What follows is what survived, in the order it survived in.\n";
                }
                out << "\n";

                // Where tracks went missing, said in the file rather than left implicit in a column
                // nobody will query. This is the whole reason the mix's own positions are kept:
                // "three tracks are gone" is much less use than "three are gone from between these
                // two", and this file is the one a person actually opens.
                const auto noteGap = [&out](int firstMissing, int lastMissing)
                {
                    out << std::format("# --- {} track(s) missing here, at position(s) {}..{} of the original mix ---\n",
                        lastMissing - firstMissing + 1,
                        firstMissing,
                        lastMissing);
                };

                // A hole before the first survivor is a hole like any other, and is invisible to a
                // comparison between neighbours - there is nothing to the left of the first entry to
                // compare it with. Losing the opening tracks of a mix is not the rarest way to lose
                // tracks, so it would be an odd one to leave out.
                if (entries.front().sourceOrderInMix.value_or(0) > 0)
                {
                    noteGap(0, *entries.front().sourceOrderInMix - 1);
                }

                for (size_t i = 0; i < entries.size(); ++i)
                {
                    const auto &entry = entries[i];

                    if (i > 0 && entry.sourceOrderInMix.has_value() && entries[i - 1].sourceOrderInMix.has_value() &&
                        *entry.sourceOrderInMix > *entries[i - 1].sourceOrderInMix + 1)
                    {
                        noteGap(*entries[i - 1].sourceOrderInMix + 1, *entry.sourceOrderInMix - 1);
                    }

                    // Rounded, not truncated: #EXTINF is what a player shows, and a 3.9 second track
                    // reading as 3 is worse than reading as 4. The exact value is on the #JADURATION
                    // line below, so nothing is lost by rounding this one.
                    const auto seconds = std::chrono::round<std::chrono::seconds>(entry.duration).count();

                    out << std::format("#EXTINF:{},{}\n", seconds, describe(entry));
                    out << std::format("#JAALBUM:{}\n", entry.albumTitle);
                    if (starts[i].has_value())
                    {
                        out << std::format("#JASTART:{}\n", starts[i]->count());
                    }
                    out << std::format("#JADURATION:{}\n", entry.duration.count());
                    out << std::format("#JASIZE:{}\n", entry.filesizeBytes);
                    out << std::format("#JATRACKID:{}\n", entry.trackId);

                    // pathToString throughout, never path::string(): the narrow form of a Windows path
                    // goes through the active code page, which mangles or throws on this library's
                    // Cyrillic, Greek and fraktur filenames.
                    const auto fullPath = std::filesystem::path{entry.folderPath} / entry.filename;
                    out << pathToString(fullPath) << "\n\n";
                }

                // Closed here and then inspected, rather than left to the destructor.
                //
                // good() with the stream still open says nothing about the write that has not happened
                // yet: the last buffer is flushed by close, and a disk that fills up does it there. The
                // destructor would swallow that, and the file - short by one buffer and looking
                // complete - would then be published.
                out.flush();
                out.close();
                if (!out)
                {
                    return std::format("writing {} failed part way through", pathToString(tempPath));
                }
            }

            if (mode == M3UWriteMode::NeverReplace)
            {
                // The name is claimed, not checked. exists() followed by a write is a race, and it
                // reports on whatever a symlink points at rather than on the name - so a dangling
                // one looks free and the write destroys it. create_hard_link fails when the name is
                // taken, atomically, whatever is under it. The same reasoning, and the same call,
                // as DatabaseBackupManager uses to reserve a backup filename.
                std::error_code linkEc;
                std::filesystem::create_hard_link(tempPath, targetPath, linkEc);
                if (linkEc)
                {
                    if (targetExistedOut != nullptr)
                    {
                        *targetExistedOut = true;
                    }

                    // Reported rather than assumed: every other reason the link could fail is a
                    // genuine failure, and saying "it was already there" about a full disk would be
                    // the wrong answer entirely. The same three-way answer as the check above, since
                    // another instance can have put anything there in the meantime.
                    if (mixRecoveryM3UTargetState(targetPath) != M3UTargetState::HoldsFile)
                    {
                        if (targetExistedOut != nullptr)
                        {
                            *targetExistedOut = false;
                        }
                        return std::format("could not put {} in place: {}", pathToString(targetPath), linkEc.message());
                    }

                    // Left exactly as it was found. The temporary goes with the guard.
                    spdlog::info("[MixRecoveryM3U] {} is already there; left as it was.", pathToString(targetPath));
                    return {};
                }

                // The link is the file now, and it is complete. The temporary is a second name for
                // the same bytes, and leaving it behind is not untidiness: anything that opens that
                // name for writing truncates the published playlist through it. So it is removed
                // here and a failure to remove it is reported, rather than swallowed by the guard.
                tempGuard.armed = false;

                std::error_code unlinkEc;
                std::filesystem::remove(tempPath, unlinkEc);
                if (unlinkEc)
                {
                    return std::format("wrote {} but could not remove the temporary {}, which is now a second name for it: {}",
                        pathToString(targetPath),
                        pathToString(tempPath),
                        unlinkEc.message());
                }

                spdlog::info("[MixRecoveryM3U] Wrote {} ({} tracks).", pathToString(targetPath), entries.size());
                return {};
            }

            // JUCE rather than std::filesystem::rename: rename does not reliably replace an existing
            // destination on Windows. replaceFileIn uses Win32 ReplaceFile where the target exists and a
            // plain move where it does not, so the previous file survives a failure either way.
            const juce::File temp{ui::jucePathFromFs(tempPath)};
            const juce::File target{ui::jucePathFromFs(targetPath)};
            if (!temp.replaceFileIn(target))
            {
                return std::format("could not put {} in place", pathToString(targetPath));
            }
            tempGuard.armed = false; // the replace consumed it

            spdlog::info("[MixRecoveryM3U] Wrote {} ({} tracks).", pathToString(targetPath), entries.size());
            return {};
        }

    } // namespace audio
} // namespace jucyaudio
