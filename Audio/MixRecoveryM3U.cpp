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

#include <chrono>
#include <format>
#include <fstream>
#include <locale>
#include <optional>

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

        std::filesystem::path companionM3UPathFor(const std::filesystem::path &audioPath)
        {
            auto companion = audioPath;
            companion.replace_extension(".m3u");
            return companion;
        }

        std::string writeMixRecoveryM3U(const std::filesystem::path &targetPath,
            const std::vector<database::MixRecoveryEntry> &entries,
            std::optional<Duration_t> totalDuration)
        {
            if (entries.empty())
            {
                return "there is nothing to write - the recovery record is empty";
            }

            // Alongside the target, not in a system temp directory: a replace across volumes is not
            // atomic, and the destination directory is the one place guaranteed to be on the same one.
            const auto tempPath = std::filesystem::path{targetPath}.replace_extension(".m3u.tmp");

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
                out << "#EXTMIX:" << entries.front().mixName << "\n";
                // Left out entirely when the record does not know it. A record written before the length
                // was stored has no answer, and printing nought would be an answer - a reader would take
                // a two-hour mix for an empty one.
                if (totalDuration.has_value())
                {
                    out << "#EXTMIXDURATION:" << std::chrono::duration_cast<std::chrono::seconds>(*totalDuration).count() << "\n";
                }
                out << "\n";

                for (size_t i = 0; i < entries.size(); ++i)
                {
                    const auto &entry = entries[i];

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

                if (!out.good())
                {
                    return std::format("writing {} failed part way through", pathToString(tempPath));
                }
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
