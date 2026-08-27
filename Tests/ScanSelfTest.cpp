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

#include <Tests/ScanSelfTest.h>

#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackQueryArgs.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <format>
#include <map>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace tests
    {
        using namespace database;

        namespace
        {
            constexpr const char *kAlbumFolder = "AlbumOne";
            constexpr int kTrackCount = 4;
            constexpr int kFixtureDurationMs = 100;
            constexpr const char *kMixName = "SelfTest Mix";
            constexpr const char *kRootEnvVar = "JUCYAUDIO_SELFTEST_ROOT";
            constexpr const char *kSentinelFile = ".jucyaudio-selftest";
            constexpr const char *kWorkingSetName = "SelfTest Working Set";

            /// @brief Collects check results so a failure does not stop the run - later checks usually
            /// explain the earlier one, and a single pass should say everything it can.
            class Report final
            {
            public:
                void check(bool passed, const std::string &what)
                {
                    m_lines.push_back(std::format("{}  {}", passed ? "PASS" : "FAIL", what));
                    if (passed)
                    {
                        spdlog::info("[SelfTest] PASS  {}", what);
                    }
                    else
                    {
                        ++m_failures;
                        spdlog::error("[SelfTest] FAIL  {}", what);
                    }
                }

                void note(const std::string &text)
                {
                    m_lines.push_back(std::format("....  {}", text));
                    spdlog::info("[SelfTest] {}", text);
                }

                /// @brief Record why the run stopped early. Used only where continuing would test
                /// nothing - the caller returns straight after.
                void abort(const std::string &why)
                {
                    ++m_failures;
                    m_lines.push_back(std::format("STOP  {}", why));
                    spdlog::error("[SelfTest] STOP  {}", why);
                }

                int failures() const
                {
                    return m_failures;
                }

                const std::vector<std::string> &lines() const
                {
                    return m_lines;
                }

            private:
                std::vector<std::string> m_lines;
                int m_failures{0};
            };

            /// @brief Writes a small but structurally valid 16-bit mono WAV of silence.
            ///
            /// Generated rather than copied so the test carries its own fixtures and depends on nothing
            /// in the user's library. WAV rather than MP3 because a correct file can be written by hand
            /// in a few lines - the scanner accepts *.wav, and TagLib reads it without complaint.
            bool writeSilentWav(const std::filesystem::path &path, uint32_t sampleCount)
            {
                const uint32_t sampleRate = 44100;
                const uint16_t channels = 1;
                const uint16_t bitsPerSample = 16;
                const uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
                const uint32_t dataBytes = sampleCount * channels * (bitsPerSample / 8);

                std::ofstream out{path, std::ios::binary};
                if (!out)
                {
                    return false;
                }

                const auto u32 = [&out](uint32_t v)
                {
                    // Written byte-by-byte rather than by dumping the struct: WAV is little-endian by
                    // definition, and this keeps that true regardless of the host.
                    const unsigned char bytes[4]{static_cast<unsigned char>(v & 0xFF),
                        static_cast<unsigned char>((v >> 8) & 0xFF),
                        static_cast<unsigned char>((v >> 16) & 0xFF),
                        static_cast<unsigned char>((v >> 24) & 0xFF)};
                    out.write(reinterpret_cast<const char *>(bytes), 4);
                };
                const auto u16 = [&out](uint16_t v)
                {
                    const unsigned char bytes[2]{static_cast<unsigned char>(v & 0xFF), static_cast<unsigned char>((v >> 8) & 0xFF)};
                    out.write(reinterpret_cast<const char *>(bytes), 2);
                };

                out.write("RIFF", 4);
                u32(36 + dataBytes);
                out.write("WAVE", 4);
                out.write("fmt ", 4);
                u32(16);       // PCM chunk size
                u16(1);        // PCM
                u16(channels);
                u32(sampleRate);
                u32(byteRate);
                u16(static_cast<uint16_t>(channels * (bitsPerSample / 8))); // block align
                u16(bitsPerSample);
                out.write("data", 4);
                u32(dataBytes);

                const std::vector<char> silence(dataBytes, 0);
                out.write(silence.data(), static_cast<std::streamsize>(silence.size()));

                return out.good();
            }

            /// @brief Every track under a folder, keyed by filename, so checks can be written by name.
            std::map<std::string, TrackInfo> tracksUnder(ITrackDatabase &db, FolderId folderId)
            {
                TrackQueryArgs args{};
                args.folderIds = {folderId};
                args.recursive = true;
                args.usePaging = false;

                std::map<std::string, TrackInfo> byName;
                for (auto &track : db.getTracks(args))
                {
                    byName[track.filename] = track;
                }
                return byName;
            }

            int countMissing(const std::map<std::string, TrackInfo> &tracks)
            {
                int n = 0;
                for (const auto &entry : tracks)
                {
                    n += entry.second.is_missing ? 1 : 0;
                }
                return n;
            }

            std::map<std::string, TrackId> idsOf(const std::map<std::string, TrackInfo> &tracks)
            {
                std::map<std::string, TrackId> ids;
                for (const auto &entry : tracks)
                {
                    ids[entry.first] = entry.second.trackId;
                }
                return ids;
            }

            std::map<std::string, TrackId> workingSetMembers(ITrackDatabase &db, WorkingSetId workingSetId)
            {
                TrackQueryArgs args{};
                args.workingSetId = workingSetId;
                args.usePaging = false;

                std::map<std::string, TrackId> ids;
                for (const auto &track : db.getTracks(args))
                {
                    ids[track.filename] = track.trackId;
                }
                return ids;
            }

            /// @brief Runs one scan to completion. Synchronous - there is no task thread here.
            bool runScan(std::vector<FolderId> folderIds, bool removeMissingFiles, Report &report, const std::string &label)
            {
                report.note(std::format("scan: {}", label));
                bool scanReportedSuccess = false;
                theTrackLibrary.scanLibrary(
                    folderIds,
                    false, // forceRescanAllFiles - see X1; the forced path has its own defect and is not under test
                    removeMissingFiles,
                    nullptr, // no progress reporting; nothing is watching
                    [&scanReportedSuccess](bool success, const std::string &message)
                    {
                        scanReportedSuccess = success;
                        spdlog::info("[SelfTest] scan finished: success={}, message='{}'", success, message);
                    },
                    nullptr);
                return scanReportedSuccess;
            }

            void writeResultsFile(const std::filesystem::path &path, const Report &report)
            {
                std::ofstream out{path, std::ios::trunc};
                if (!out)
                {
                    spdlog::error("[SelfTest] Could not write results to {}", pathToString(path));
                    return;
                }

                out << "jucyaudio scan self test\n";
                out << (report.failures() == 0 ? "RESULT: PASS\n" : std::format("RESULT: FAIL ({} failed)\n", report.failures()));
                out << "\n";
                for (const auto &line : report.lines())
                {
                    out << line << "\n";
                }
            }
        } // namespace

        std::string prepareSelfTestEnvironment(std::filesystem::path &rootOut, std::filesystem::path &databasePathOut)
        {
            const char *const configured = std::getenv(kRootEnvVar);
            if (configured == nullptr || configured[0] == 0)
            {
                return std::format("{} is not set. This mode deletes and rewrites whatever it is pointed at, so it only runs"
                                   " against a directory named for that purpose - `just selftest` sets it up.",
                    kRootEnvVar);
            }

            const auto root = expandPath(configured);
            const auto sentinel = root / kSentinelFile;

            // Every filesystem call below gets its own error_code. Sharing one lets a later success
            // clear an earlier failure - a failed exists() followed by a working create_directories()
            // would report nothing at all.
            std::error_code rootExistsEc;
            const bool rootExists = std::filesystem::exists(root, rootExistsEc);
            if (rootExistsEc)
            {
                return std::format("Could not inspect {}: {}", pathToString(root), rootExistsEc.message());
            }

            std::error_code sentinelExistsEc;
            const bool hasSentinel = std::filesystem::exists(sentinel, sentinelExistsEc);
            if (sentinelExistsEc)
            {
                return std::format("Could not inspect {}: {}", pathToString(sentinel), sentinelExistsEc.message());
            }

            if (rootExists && !hasSentinel)
            {
                // Empty is fine - a fresh directory has nothing to lose. Anything else without our
                // sentinel is somebody's real directory, and the first thing this test does is empty it.
                std::error_code emptyEc;
                const bool empty = std::filesystem::is_empty(root, emptyEc);
                if (emptyEc)
                {
                    return std::format("Could not inspect {}: {}", pathToString(root), emptyEc.message());
                }
                if (!empty)
                {
                    return std::format("{} points at {}, which is not empty and carries no {} marker. Refusing to clear a"
                                       " directory this test did not create.",
                        kRootEnvVar, pathToString(root), kSentinelFile);
                }
            }

            std::error_code makeRootEc;
            std::filesystem::create_directories(root, makeRootEc);
            if (makeRootEc)
            {
                return std::format("Could not create the self test root {}: {}", pathToString(root), makeRootEc.message());
            }

            {
                std::ofstream marker{sentinel, std::ios::trunc};
                if (!marker)
                {
                    return std::format("Could not write the marker file {}", pathToString(sentinel));
                }
                marker << "Created by the jucyaudio scan self test. Everything in this directory is disposable." << std::endl;
            }

            // The database gets a directory of its own, removed whole. Deleting just the .db file would
            // leave selftest.db-wal and selftest.db-shm behind after an interrupted run - the database
            // runs in WAL mode (SqliteTrackDatabase.cpp:617) - and those carry committed pages, so the
            // "new" library would start with the previous run's tracks in it.
            //
            // Never the configured database either: settings may name a production path outright, and
            // opening one is not a read-only act, it can run schema migrations.
            const auto databaseDir = root / "db";

            std::error_code removeDbEc;
            std::filesystem::remove_all(databaseDir, removeDbEc);
            if (removeDbEc)
            {
                return std::format("Could not remove the previous self test database directory {}: {}", pathToString(databaseDir), removeDbEc.message());
            }

            std::error_code makeDbEc;
            std::filesystem::create_directories(databaseDir, makeDbEc);
            if (makeDbEc)
            {
                return std::format("Could not create the self test database directory {}: {}", pathToString(databaseDir), makeDbEc.message());
            }

            rootOut = root;
            databasePathOut = databaseDir / "selftest.db";
            return {};
        }

        int runScanSelfTest(const std::filesystem::path &selfTestRoot)
        {
            Report report;
            const auto workRoot = selfTestRoot / "library";
            // Deliberately a sibling of workRoot, not a child. The scan is recursive, so an album parked
            // inside the scanned tree would simply be rediscovered under its new name: four extra tracks
            // inserted, the originals still flagged, and the test proving the opposite of what it claims.
            const auto awayRoot = selfTestRoot / "away";
            const auto resultsPath = selfTestRoot / "selftest-results.txt";

            spdlog::info("[SelfTest] Starting scan self test. Root: {}", pathToString(selfTestRoot));

            // Each step gets its own error_code and its own check. Sharing one across consecutive calls
            // means a success quietly clears the failure before it, and the run continues on a tree that
            // was never cleaned.
            const auto cleanUp = [&report](const std::filesystem::path &path) -> bool
            {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
                if (ec)
                {
                    report.abort(std::format("Could not clear {}: {}", pathToString(path), ec.message()));
                    return false;
                }
                return true;
            };
            const auto makeDirectory = [&report](const std::filesystem::path &path) -> bool
            {
                std::error_code ec;
                std::filesystem::create_directories(path, ec);
                if (ec)
                {
                    report.abort(std::format("Could not create {}: {}", pathToString(path), ec.message()));
                    return false;
                }
                return true;
            };

            const auto albumPath = workRoot / kAlbumFolder;
            const auto awayPath = awayRoot / kAlbumFolder;

            // Both trees, not just the scanned one: a previous run interrupted mid-cycle leaves the album
            // parked in awayRoot, and moving it back would then fail on an existing directory.
            if (!cleanUp(workRoot) || !cleanUp(awayRoot) || !makeDirectory(awayRoot) || !makeDirectory(albumPath))
            {
                writeResultsFile(resultsPath, report);
                return 1;
            }

            for (int i = 1; i <= kTrackCount; ++i)
            {
                const auto file = albumPath / std::format("track{:02}.wav", i);
                // long enough to be a real file, short enough to be instant
                if (!writeSilentWav(file, static_cast<uint32_t>(44100 * kFixtureDurationMs / 1000)))
                {
                    report.abort(std::format("Could not write the fixture {}", pathToString(file)));
                    writeResultsFile(resultsPath, report);
                    return 1;
                }
            }
            report.note(std::format("built a {}-track scratch library at {}", kTrackCount, pathToString(albumPath)));

            auto &db = theTrackLibrary.getTrackDatabase();
            if (!db.getLibraryRootManager().addRoot(pathToString(workRoot)).has_value())
            {
                report.abort("Could not add the scratch library as a library root.");
                writeResultsFile(resultsPath, report);
                return 1;
            }

            const auto rootFolderId = db.getFolderDatabase().findOrCreateFolderByPath(workRoot);
            if (rootFolderId <= 0)
            {
                report.abort("Could not resolve a FolderId for the scratch library root.");
                writeResultsFile(resultsPath, report);
                return 1;
            }

            // --- 1. First scan: the tracks are discovered and none of them is missing. ---

            report.check(runScan({rootFolderId}, false, report, "initial discovery"), "initial scan reports success");

            auto tracks = tracksUnder(db, rootFolderId);
            report.check(static_cast<int>(tracks.size()) == kTrackCount, std::format("{} tracks discovered (found {})", kTrackCount, tracks.size()));
            report.check(countMissing(tracks) == 0, "no track is flagged missing after discovery");

            const auto originalIds = idsOf(tracks);
            if (static_cast<int>(originalIds.size()) != kTrackCount)
            {
                report.abort("Discovery did not produce the expected tracks; the rest of the test would prove nothing.");
                writeResultsFile(resultsPath, report);
                return 1;
            }

            // Real rows in MixTracks and WorkingSetTracks, created now so the missing/recovery cycle has
            // something to break. Stable ids alone would not prove these survived - nothing would have
            // referenced the tracks at all.
            std::vector<TrackInfo> trackInfos;
            std::vector<MixTrack> mixTracks;
            for (const auto &entry : tracks)
            {
                trackInfos.push_back(entry.second);

                MixTrack mixTrack{};
                mixTrack.trackId = entry.second.trackId;
                mixTrack.orderInMix = static_cast<int>(mixTracks.size());
                mixTracks.push_back(mixTrack);
            }

            MixInfo mixInfo{};
            mixInfo.name = kMixName;
            // createOrUpdateMix asserts that a non-empty mix carries a duration, so a zero here aborts a
            // Debug run outright. The value only has to be positive and plausible - nothing in this test
            // reads it back - so the fixtures' own length will do.
            mixInfo.totalDuration = Duration_t{kTrackCount * kFixtureDurationMs};
            const bool mixCreated = theTrackLibrary.getMixManager().createOrUpdateMix(mixInfo, mixTracks);
            report.check(mixCreated && mixInfo.mixId > 0, std::format("created a mix referencing all {} tracks", kTrackCount));

            WorkingSetInfo workingSetInfo{};
            const bool workingSetCreated =
                theTrackLibrary.getWorkingSetManager().createWorkingSetFromTrackInfos(trackInfos, kWorkingSetName, workingSetInfo);
            report.check(workingSetCreated && workingSetInfo.id > 0, std::format("created a working set holding all {} tracks", kTrackCount));

            if (!mixCreated || !workingSetCreated)
            {
                report.abort("Could not create the referencing rows; the recovery checks would prove nothing.");
                writeResultsFile(resultsPath, report);
                return 1;
            }

            // --- 2. The folder goes away: every track is flagged, and nothing is deleted. ---

            {
                std::error_code renameEc;
                std::filesystem::rename(albumPath, awayPath, renameEc);
                if (renameEc)
                {
                    report.abort(std::format("Could not move the album folder aside: {}", renameEc.message()));
                    writeResultsFile(resultsPath, report);
                    return 1;
                }
            }
            report.note("moved the album folder aside");

            report.check(runScan({rootFolderId}, false, report, "folder missing"), "scan reports success with the folder gone");

            tracks = tracksUnder(db, rootFolderId);
            report.check(countMissing(tracks) == kTrackCount, std::format("all {} tracks flagged missing (flagged {})", kTrackCount, countMissing(tracks)));
            report.check(static_cast<int>(tracks.size()) == kTrackCount, "no track row was deleted when the file vanished");
            report.check(idsOf(tracks) == originalIds, "track ids are unchanged after being flagged");

            // --- 3. Scanning again with the folder still gone changes nothing (B4). ---

            report.check(runScan({rootFolderId}, false, report, "folder still missing"), "repeat scan reports success");

            tracks = tracksUnder(db, rootFolderId);
            report.check(countMissing(tracks) == kTrackCount, "tracks are still flagged after a repeat scan");
            report.check(idsOf(tracks) == originalIds, "track ids survive a repeat scan");
            report.note("the log line above should read '0 newly missing ... (4 were already flagged)' - that is B4's skip");

            // --- 4. The folder comes back: the flag clears and the rows are the same rows. ---

            {
                std::error_code renameEc;
                std::filesystem::rename(awayPath, albumPath, renameEc);
                if (renameEc)
                {
                    report.abort(std::format("Could not move the album folder back: {}", renameEc.message()));
                    writeResultsFile(resultsPath, report);
                    return 1;
                }
            }
            report.note("moved the album folder back");

            report.check(runScan({rootFolderId}, false, report, "folder restored"), "scan reports success with the folder back");

            tracks = tracksUnder(db, rootFolderId);
            report.check(countMissing(tracks) == 0, std::format("no track is flagged any more (still flagged: {})", countMissing(tracks)));

            // The point of the whole exercise.
            report.check(static_cast<int>(tracks.size()) == kTrackCount,
                std::format("still exactly {} track rows - recovery did not insert duplicates (found {})", kTrackCount, tracks.size()));
            report.check(idsOf(tracks) == originalIds, "every recovered track kept its original track_id");

            // Read the references back rather than inferring them from the ids. Both tables cascade on
            // track deletion, so had recovery gone through a delete-and-reinsert these would be empty
            // even though the track count looked right.
            const auto survivingMixTracks = theTrackLibrary.getMixManager().getMixTracks(mixInfo.mixId);
            report.check(static_cast<int>(survivingMixTracks.size()) == kTrackCount,
                std::format("the mix still lists {} tracks (lists {})", kTrackCount, survivingMixTracks.size()));

            std::map<std::string, TrackId> mixIdsByName;
            for (const auto &mixTrack : survivingMixTracks)
            {
                for (const auto &entry : tracks)
                {
                    if (entry.second.trackId == mixTrack.trackId)
                    {
                        mixIdsByName[entry.first] = mixTrack.trackId;
                    }
                }
            }
            report.check(mixIdsByName == originalIds, "the mix still points at the same track ids it was built from");

            report.check(workingSetMembers(db, workingSetInfo.id) == originalIds,
                std::format("the working set still holds the same {} tracks", kTrackCount));

            writeResultsFile(resultsPath, report);
            spdlog::info("[SelfTest] Finished with {} failure(s). Results: {}", report.failures(), pathToString(resultsPath));
            return report.failures() == 0 ? 0 : 1;
        }

    } // namespace tests
} // namespace jucyaudio
