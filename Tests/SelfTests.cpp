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

#include <Tests/SelfTests.h>

#include <Audio/Includes/ActiveExportSettings.h>
#include <Audio/MixExporter.h>
#include <Audio/MixRecoveryM3U.h>
#include <Database/Includes/AlbumInfo.h>
#include <Database/Includes/IAlbumManager.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/MixRecoveryEntry.h>
#include <Database/Includes/TrackQueryArgs.h>
#include <Database/DatabaseBackupManager.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/TrackLibrary.h>
#include <UI/Settings.h>
#include <UI/TimelineComponent.h>
#include <Utils/AssortedUtils.h>

#include <nlohmann/json.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <format>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace jucyaudio
{
    namespace tests
    {
        using namespace database;
        using json = nlohmann::json;

        namespace
        {
            constexpr const char *kAlbumFolder = "AlbumOne";
            constexpr int kTrackCount = 4;
            // Whole seconds, and at least one. TagLib reports length through lengthInSeconds(), so
            // anything under a second scans as a duration of zero - which is what every fixture used
            // to be. Nothing noticed until a test needed the scanned value rather than this constant.
            constexpr int kFixtureDurationMs = 2000;
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

            /// @brief Every track row under a folder, unkeyed.
            ///
            /// tracksUnder keys by filename, which is right for comparing identity across a scan and
            /// wrong for counting: two files of the same name in different folders collapse into one
            /// entry, so a check that a duplicate row was inserted would read as though it had not been.
            std::vector<TrackInfo> trackRowsUnder(ITrackDatabase &db, FolderId folderId)
            {
                TrackQueryArgs args{};
                args.folderIds = {folderId};
                args.recursive = true;
                args.usePaging = false;
                return db.getTracks(args);
            }

            int countMissing(const std::vector<TrackInfo> &tracks)
            {
                int n = 0;
                for (const auto &track : tracks)
                {
                    n += track.is_missing ? 1 : 0;
                }
                return n;
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

            /// @brief "0, 2, 3" - so a failing position check says what it actually found.
            std::string idsToText(const std::vector<int> &values)
            {
                std::string text;
                for (const auto value : values)
                {
                    text += (text.empty() ? "" : ", ") + std::to_string(value);
                }
                return text;
            }

            /// @brief How many playlist temporaries are lying about in a directory.
            ///
            /// The temporary is named uniquely per attempt, so no single path can be tested for. A
            /// leftover is worse than untidy: after the link it is a second name for the published
            /// playlist, and opening it for writing truncates what was published.
            int strayTempCount(const std::filesystem::path &directory)
            {
                // Advanced by hand for the same reason the backfill does it: a range-for calls the
                // throwing operator++, and a construction error would otherwise report zero strays -
                // a clean answer from a check that never ran.
                int strays = 0;
                std::error_code listEc;
                std::filesystem::directory_iterator entry{directory, listEc};
                const std::filesystem::directory_iterator end;

                while (!listEc && entry != end)
                {
                    if (entry->path().extension() == ".m3utmp")
                    {
                        ++strays;
                    }
                    entry.increment(listEc);
                }

                // A failure to look is not the same as nothing being there, and this feeds an assertion
                // that something is absent. Reported as one stray so that check fails rather than passes.
                return listEc ? strays + 1 : strays;
            }

            /// @brief Are these two recovery records identical in every field?
            ///
            /// Row counts are not enough: a rewrite that happens to produce the same number of rows would
            /// pass a size check while having replaced everything. capturedAt is the giveaway - it is
            /// stamped fresh on every capture, so any rewrite at all changes it.
            bool sameRecord(const std::vector<MixRecoveryEntry> &a, const std::vector<MixRecoveryEntry> &b)
            {
                if (a.size() != b.size())
                {
                    return false;
                }
                for (size_t i = 0; i < a.size(); ++i)
                {
                    if (a[i].mixId != b[i].mixId || a[i].orderInMix != b[i].orderInMix || a[i].capturedAt != b[i].capturedAt ||
                        a[i].mixName != b[i].mixName || a[i].mixTotalDuration != b[i].mixTotalDuration ||
                        a[i].trackId != b[i].trackId || a[i].artistName != b[i].artistName ||
                        a[i].albumTitle != b[i].albumTitle || a[i].title != b[i].title || a[i].filename != b[i].filename ||
                        a[i].folderPath != b[i].folderPath || a[i].duration != b[i].duration || a[i].filesizeBytes != b[i].filesizeBytes ||
                        a[i].bpm != b[i].bpm || a[i].mixData != b[i].mixData || a[i].isComplete != b[i].isComplete ||
                        a[i].sourceOrderInMix != b[i].sourceOrderInMix)
                    {
                        return false;
                    }
                }
                return true;
            }

            /// @brief Writes a JSON field the MixTrack parser knows nothing about into one mix_data row.
            ///
            /// The whole point of storing mix_data verbatim is that a field nobody understands today
            /// still survives. Nothing reachable through the public interfaces can produce such a field -
            /// mix_data only ever gets written by createOrUpdateMix, which serialises with the same
            /// to_json a re-serialising implementation would use, so both would emit identical bytes and
            /// no assertion could tell them apart.
            ///
            /// So the fixture is staged over the test's own connection to the scratch database, opened
            /// and closed here and used for nothing else. This touches no production code, and the
            /// assertion that follows goes entirely through public interfaces.
            ///
            /// @return The exact text written, to compare the recovery row against, or empty on failure.
            std::string injectUnknownFieldIntoMixData(const std::filesystem::path &databasePath, MixId mixId, int orderInMix)
            {
                // Scoped: the destructor closes it, so the fixture connection is gone before anything
                // else in the test runs.
                SqliteDatabase fixtureDb;
                if (!fixtureDb.open(pathToString(databasePath)))
                {
                    return {};
                }

                std::string original;
                {
                    SqliteStatement read{fixtureDb};
                    if (!read.query(
                            [&original, &read]() -> bool
                            {
                                original = read.getText(0);
                                return true;
                            },
                            "SELECT mix_data FROM MixTracks WHERE mix_id=? AND order_in_mix=?",
                            mixId,
                            orderInMix))
                    {
                        return {};
                    }
                }

                if (original.empty())
                {
                    return {};
                }

                std::string doctored;
                try
                {
                    auto parsed = json::parse(original);
                    parsed["selfTestUnknownField"] = "kept verbatim or not at all";
                    doctored = parsed.dump();
                }
                catch (const std::exception &)
                {
                    return {};
                }

                {
                    SqliteStatement write{fixtureDb, "UPDATE MixTracks SET mix_data=? WHERE mix_id=? AND order_in_mix=?"};
                    if (!write.isValid() || !write.addParam(doctored) || !write.addParam(mixId) || !write.addParam(orderInMix) || !write.execute())
                    {
                        return {};
                    }
                }

                return doctored;
            }

            /// @brief Puts unparseable text into one row's mix_data.
            ///
            /// The row stays, the mix keeps its shape, and only that one row becomes unreadable - which
            /// is the shape of real corruption and the one case a partial read turns into data loss.
            bool corruptMixData(const std::filesystem::path &databasePath, MixId mixId, int orderInMix)
            {
                SqliteDatabase fixtureDb;
                if (!fixtureDb.open(pathToString(databasePath)))
                {
                    return false;
                }

                SqliteStatement stmt{fixtureDb, "UPDATE MixTracks SET mix_data = ? WHERE mix_id = ? AND order_in_mix = ?"};
                if (!stmt.isValid() || !stmt.addParam(std::string{"{not json at all"}) || !stmt.addParam(mixId) || !stmt.addParam(orderInMix) ||
                    !stmt.execute())
                {
                    return false;
                }

                // execute() succeeds whether or not any row matched, and a fixture that quietly
                // changes nothing is worse than one that fails: every check built on it then passes
                // by describing a mix that was never corrupted. This one did exactly that.
                return fixtureDb.getChangesCount() == 1;
            }

            /// @brief Moves one mix row on top of another, so two share a position.
            ///
            /// MixTracks has only an index on (mix_id, order_in_mix), not a unique constraint, so this
            /// is a state the table genuinely allows and two mixes in the real library are in. Nothing
            /// reachable through the interfaces produces it - removeTracksFromMix renumbers - which is
            /// why it went unnoticed until a capture tried to copy those positions into MixRecovery,
            /// where they are the primary key.
            bool duplicateOrderInMix(const std::filesystem::path &databasePath, MixId mixId, int fromOrder, int toOrder)
            {
                SqliteDatabase fixtureDb;
                if (!fixtureDb.open(pathToString(databasePath)))
                {
                    return false;
                }

                SqliteStatement stmt{fixtureDb, "UPDATE MixTracks SET order_in_mix = ? WHERE mix_id = ? AND order_in_mix = ?"};
                if (!stmt.isValid() || !stmt.addParam(toOrder) || !stmt.addParam(mixId) || !stmt.addParam(fromOrder) || !stmt.execute())
                {
                    return false;
                }
                return fixtureDb.getChangesCount() == 1;
            }

            /// @brief Removes a mix's recovery record, so the mix looks like one that never had one.
            ///
            /// Over the test's own connection: nothing in the interface deletes a record on its own,
            /// which is deliberate - a record is meant to outlive the mix data it describes.
            bool clearRecoveryData(const std::filesystem::path &databasePath, MixId mixId)
            {
                SqliteDatabase fixtureDb;
                if (!fixtureDb.open(pathToString(databasePath)))
                {
                    return false;
                }

                SqliteStatement stmt{fixtureDb, "DELETE FROM MixRecovery WHERE mix_id = ?"};
                return stmt.isValid() && stmt.addParam(mixId) && stmt.execute();
            }

            /// @brief Writes deliberately wrong summary columns for one mix.
            ///
            /// Over the test's own connection because nothing reachable through the public interfaces
            /// can do this any more: the columns are derived on every write and there is no setter. That
            /// is the fix, and it is also why the repair that corrects such a row cannot be exercised
            /// without reaching past the interfaces to create one.
            bool setMixSummary(const std::filesystem::path &databasePath, MixId mixId, int64_t trackCount, int64_t totalLength)
            {
                SqliteDatabase fixtureDb;
                if (!fixtureDb.open(pathToString(databasePath)))
                {
                    return false;
                }

                SqliteStatement stmt{fixtureDb, "UPDATE Mixes SET track_count = ?, total_length = ? WHERE mix_id = ?"};
                return stmt.isValid() && stmt.addParam(trackCount) && stmt.addParam(totalLength) && stmt.addParam(mixId) && stmt.execute();
            }

            /// @brief Clears total_duration on a mix's recovery rows, as a v27 record would have it.
            ///
            /// Staged over the test's own connection for the same reason the unknown JSON field is:
            /// nothing reachable through the public interfaces can produce a NULL there any more, and the
            /// case still has to be exercised because the database on disk is full of rows that will.
            bool setRecoveryDurationToNull(const std::filesystem::path &databasePath, MixId mixId)
            {
                SqliteDatabase fixtureDb;
                if (!fixtureDb.open(pathToString(databasePath)))
                {
                    return false;
                }

                SqliteStatement stmt{fixtureDb, "UPDATE MixRecovery SET total_duration = NULL WHERE mix_id = ?"};
                return stmt.isValid() && stmt.addParam(mixId) && stmt.execute();
            }

            /// @brief Runs one scan to completion. Synchronous - there is no task thread here.
            bool runScan(std::vector<FolderId> folderIds,
                bool removeMissingFiles,
                Report &report,
                const std::string &label,
                bool forceRescanAllFiles = false)
            {
                report.note(std::format("scan: {}", label));
                bool scanReportedSuccess = false;
                theTrackLibrary.scanLibrary(
                    folderIds,
                    forceRescanAllFiles,
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

            /// @param title Names the suite. Passed in rather than fixed, because two suites write two
            /// files and a results file that misnames itself is worse than one with no title at all.
            void writeResultsFile(const std::filesystem::path &path, const std::string &title, const Report &report)
            {
                std::ofstream out{path, std::ios::trunc};
                if (!out)
                {
                    spdlog::error("[SelfTest] Could not write results to {}", pathToString(path));
                    return;
                }

                out << title << "\n";
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

        int runScanSelfTest(const std::filesystem::path &selfTestRoot, const std::filesystem::path &databasePath)
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
                writeResultsFile(resultsPath, "jucyaudio scan self test", report);
                return 1;
            }

            for (int i = 1; i <= kTrackCount; ++i)
            {
                const auto file = albumPath / std::format("track{:02}.wav", i);
                // long enough to be a real file, short enough to be instant
                if (!writeSilentWav(file, static_cast<uint32_t>(44100 * kFixtureDurationMs / 1000)))
                {
                    report.abort(std::format("Could not write the fixture {}", pathToString(file)));
                    writeResultsFile(resultsPath, "jucyaudio scan self test", report);
                    return 1;
                }
            }
            report.note(std::format("built a {}-track scratch library at {}", kTrackCount, pathToString(albumPath)));

            auto &db = theTrackLibrary.getTrackDatabase();
            if (!db.getLibraryRootManager().addRoot(pathToString(workRoot)).has_value())
            {
                report.abort("Could not add the scratch library as a library root.");
                writeResultsFile(resultsPath, "jucyaudio scan self test", report);
                return 1;
            }

            const auto rootFolderId = db.getFolderDatabase().findOrCreateFolderByPath(workRoot);
            if (rootFolderId <= 0)
            {
                report.abort("Could not resolve a FolderId for the scratch library root.");
                writeResultsFile(resultsPath, "jucyaudio scan self test", report);
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
                writeResultsFile(resultsPath, "jucyaudio scan self test", report);
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
                writeResultsFile(resultsPath, "jucyaudio scan self test", report);
                return 1;
            }

            // --- 2. The folder goes away: every track is flagged, and nothing is deleted. ---

            {
                std::error_code renameEc;
                std::filesystem::rename(albumPath, awayPath, renameEc);
                if (renameEc)
                {
                    report.abort(std::format("Could not move the album folder aside: {}", renameEc.message()));
                    writeResultsFile(resultsPath, "jucyaudio scan self test", report);
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
                    writeResultsFile(resultsPath, "jucyaudio scan self test", report);
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

            // --- 5. An empty waveform in the cache is a miss, not a hit. ---
            //
            // Here rather than in a suite of its own because this is the same failure the four
            // steps above describe, one layer down: a track whose file was missing when the mix
            // editor drew it had an AudioThumbnail that decoded nothing written to WaveformCache.
            // JUCE serialises that as a valid 52-byte header, so it read back as a cache hit and
            // the track kept a blank waveform for good - even after the scan above cleared
            // is_missing and the file was demonstrably back. 24 tracks in the real library were
            // in exactly that state.
            //
            // The bytes are written by hand: nothing reachable through the interfaces produces
            // one any more, which is the point of the write guards.
            {
                const auto victimId = originalIds.begin()->second;

                // "jatm", samplesPerThumbSample = 2048, then a zero totalSamples - what
                // MixTrackComponent stored for a source it could not read.
                std::vector<unsigned char> tombstone(52, 0);
                tombstone[0] = 'j';
                tombstone[1] = 'a';
                tombstone[2] = 't';
                tombstone[3] = 'm';
                tombstone[5] = 0x08;

                report.check(theTrackLibrary.saveWaveform(victimId, tombstone).isOk(), "an empty waveform could be staged into the cache");

                std::vector<unsigned char> readBack{0xFF};
                const auto emptyResult = theTrackLibrary.loadWaveform(victimId, readBack);
                report.check(!emptyResult.isOk(), "an empty cached waveform reads back as a miss, so the track regenerates");
                report.check(readBack.empty(), "a rejected waveform hands back nothing rather than the empty blob");

                // The same header carrying samples must still be returned, or the guard would have
                // turned every waveform in the library into a permanent cache miss.
                auto realOne = tombstone;
                realOne[8] = 0x00;
                realOne[9] = 0xD2;
                realOne[10] = 0xE4;
                report.check(theTrackLibrary.saveWaveform(victimId, realOne).isOk(), "a waveform with samples could be staged");

                std::vector<unsigned char> goodBack;
                report.check(theTrackLibrary.loadWaveform(victimId, goodBack).isOk() && goodBack == realOne,
                    "a waveform with samples still reads back unchanged");
            }

            // --- 6. Renaming a genre reaches the vocabulary and every album that uses it. ---
            //
            // Three branches worth separating: a rename to a free name, a rename onto a name that
            // already exists (a merge, because Genres.name is UNIQUE COLLATE NOCASE and refusing
            // would decline the case people rename for), and a change of capitalisation only, which
            // is the same row under that index and so cannot be done by insert-then-delete.
            {
                auto &albums = theTrackLibrary.getAlbumManager();

                const auto albumOne = albums.findOrCreateAlbum("SelfTest Album One", rootFolderId);
                const auto albumTwo = albums.findOrCreateAlbum("SelfTest Album Two", rootFolderId);
                const auto albumBoth = albums.findOrCreateAlbum("SelfTest Album Both", rootFolderId);
                report.check(albumOne > 0 && albumTwo > 0 && albumBoth > 0, "three scratch albums could be created");

                // Into the vocabulary as well as onto the albums. renameGenre resolves both rows in
                // Genres, so names that exist only inside album JSON are not renameable at all - and
                // the merge case below turns on the target already being in the vocabulary.
                report.check(albums.addGenre("selftest-alpha") && albums.addGenre("selftest-beta"), "two scratch genres could be added to the vocabulary");

                // The third album carries both names, in this order, so the merge below has to drop
                // one of them and keep the position of the earlier.
                const std::vector<std::string> moods{"nocturnal"};
                const std::vector<std::string> tags{"selftest"};
                albums.updateAlbumMetadata(albumOne, {"selftest-alpha"}, moods, tags);
                albums.updateAlbumMetadata(albumTwo, {"selftest-beta"}, moods, tags);
                albums.updateAlbumMetadata(albumBoth, {"selftest-alpha", "selftest-beta"}, moods, tags);

                const auto genresOf = [&albums](AlbumId id)
                {
                    const auto info = albums.getAlbumById(id);
                    return info.has_value() ? info->genres : std::vector<std::string>{};
                };
                const auto vocabularyHas = [&albums](const std::string &name)
                {
                    const auto vocabulary = albums.getGenresWithUsage();
                    return std::any_of(vocabulary.begin(),
                        vocabulary.end(),
                        [&name](const GenreUsage &entry) { return entry.name == name; });
                };

                // (a) Rename to a name nothing else uses.
                bool merged = true;
                report.check(albums.renameGenre("selftest-alpha", "selftest-gamma", &merged), "a genre can be renamed to a free name");
                report.check(!merged, "renaming to a free name does not report a merge");
                report.check(vocabularyHas("selftest-gamma") && !vocabularyHas("selftest-alpha"), "the vocabulary carries the new name and not the old");
                report.check(genresOf(albumOne) == std::vector<std::string>{"selftest-gamma"}, "the album using it was relabelled");

                // The rename must not disturb the columns it was not given. updateAlbumMetadata
                // writes genres, moods and tags together, so going through it would blank two of
                // them - which is why renameGenre writes the genres column on its own.
                const auto untouched = albums.getAlbumById(albumOne);
                report.check(untouched.has_value() && untouched->moods == moods && untouched->tags == tags,
                    "renaming a genre leaves the album's moods and tags alone");

                // (b) Rename onto a name that already exists: a merge.
                merged = false;
                report.check(albums.renameGenre("selftest-gamma", "selftest-beta", &merged), "a genre can be renamed onto an existing one");
                report.check(merged, "renaming onto an existing name reports a merge");
                report.check(!vocabularyHas("selftest-gamma"), "the merged-away name is gone from the vocabulary");
                report.check(genresOf(albumOne) == std::vector<std::string>{"selftest-beta"}, "an album holding only the old name now holds the new one");
                report.check(genresOf(albumTwo) == std::vector<std::string>{"selftest-beta"}, "an album that already held the new name is unchanged");

                // The point of the merge rule: one entry, not two, and in the position the first of
                // the two occupied - the leading genre is the headline and must stay the headline.
                report.check(genresOf(albumBoth) == std::vector<std::string>{"selftest-beta"},
                    std::format("an album that held both ends up with one entry (holds {})", genresOf(albumBoth).size()));

                // (c) Capitalisation only. The two names are the same row under COLLATE NOCASE, so
                // an insert-then-delete would delete the row it had just matched.
                merged = true;
                report.check(albums.renameGenre("selftest-beta", "SelfTest-Beta", &merged), "a genre can be recased");
                report.check(!merged, "recasing is a rename, not a merge");
                report.check(vocabularyHas("SelfTest-Beta"), "the vocabulary carries the new capitalisation");
                report.check(genresOf(albumOne) == std::vector<std::string>{"SelfTest-Beta"}, "albums carry the new capitalisation too");

                // (e) A merge adopts the surviving row's spelling, not the one that was typed.
                report.check(albums.addGenre("selftest-delta"), "another scratch genre could be added");
                albums.updateAlbumMetadata(albumTwo, {"selftest-delta"}, moods, tags);
                report.check(albums.renameGenre("selftest-delta", "SELFTEST-BETA", &merged) && merged, "a genre can be merged using a different capitalisation");
                report.check(vocabularyHas("SelfTest-Beta"), "the surviving vocabulary row kept its own spelling");
                report.check(genresOf(albumTwo) == std::vector<std::string>{"SelfTest-Beta"},
                    "the relabelled album carries the surviving spelling, not the typed one");

                // (f) Refusals leave everything alone.
                report.check(!albums.renameGenre("SelfTest-Beta", "   "), "a rename to an empty name is refused");
                report.check(!albums.renameGenre("selftest-not-in-the-vocabulary", "selftest-anything"),
                    "renaming a name that is not in the vocabulary is refused");
                report.check(!albums.renameGenre("selftest-not-in-the-vocabulary", "selftest-not-in-the-vocabulary"),
                    "renaming an absent name to itself is refused too, not waved through as a no-op");
                report.check(albums.renameGenre("SelfTest-Beta", "SelfTest-Beta"), "renaming a genre to exactly its own name is a no-op, not a failure");
                report.check(!vocabularyHas("selftest-anything"), "a refused rename did not invent a vocabulary entry");
                report.check(genresOf(albumOne) == std::vector<std::string>{"SelfTest-Beta"}, "a refused rename changed nothing");

                // (g) An album that lists an unrelated genre twice is not tidied up in passing.
                report.check(albums.addGenre("selftest-epsilon"), "one more scratch genre could be added");
                albums.updateAlbumMetadata(albumBoth, {"selftest-epsilon", "selftest-epsilon"}, moods, tags);
                report.check(albums.renameGenre("SelfTest-Beta", "selftest-zeta"), "an unrelated genre can be renamed");
                report.check(genresOf(albumBoth) == std::vector<std::string>{"selftest-epsilon", "selftest-epsilon"},
                    "an album not carrying the renamed genre was left exactly as it was");

                // (h) Two names that differ only by an accented capital are two rows to SQLite, whose
                // NOCASE collation folds A-Z and nothing else, and they have to stay two throughout.
                // A Unicode-aware fold calls them one, and everything downstream then acts on
                // whichever row it happens to find - relabelling albums that belong to the other.
                //
                // Written as bytes rather than as literals so the test does not depend on how the
                // compiler was told to read this file: C3 89 is U+00C9 E-acute, C3 A9 is U+00E9.
                const std::string accentedUpper{"\xC3\x89" "lectro-selftest"};
                const std::string accentedLower{"\xC3\xA9" "lectro-selftest"};
                report.check(albums.addGenre(accentedUpper) && albums.addGenre(accentedLower),
                    "two genres differing only by an accented capital could both be added");
                report.check(vocabularyHas(accentedUpper) && vocabularyHas(accentedLower), "the vocabulary keeps them as two separate rows");

                albums.updateAlbumMetadata(albumOne, {accentedUpper}, moods, tags);
                albums.updateAlbumMetadata(albumTwo, {accentedLower}, moods, tags);
                report.check(albums.renameGenre(accentedUpper, "selftest-eta"), "one of the two can be renamed to a free name");
                report.check(genresOf(albumOne) == std::vector<std::string>{"selftest-eta"}, "the album carrying the renamed spelling was relabelled");
                report.check(genresOf(albumTwo) == std::vector<std::string>{accentedLower},
                    "the album carrying the other spelling was left alone - this is the whole point");
                report.check(vocabularyHas(accentedLower), "the other vocabulary row survives the rename");
            }

            // --- 7. Mixes.total_length is derived on every write, not carried by the caller. ---
            //
            // It used to be whatever the caller put in MixInfo, and six of the eight paths that wrote
            // a mix passed along the value they were already holding. Appending therefore stored the
            // length the mix had before the append, again and again: one five-hour mix in the real
            // library had accumulated a stored length of sixty-six hours.
            //
            // Every check below reads the figure back out of the database rather than from the
            // MixInfo that was passed in. An in-memory check would have passed throughout the entire
            // period the bug existed, because the editor recomputed for its own display and only the
            // stored column was wrong - which is why reopening a mix brought the wrong number back.
            {
                auto &mixes = theTrackLibrary.getMixManager();

                // Attach points make a mix shorter than the sum of its tracks. Without them every
                // check here would pass against a plain sum, which is one of the wrong answers.
                constexpr int64_t kOverlapMs = 20;

                // Read from a scanned fixture rather than assumed from kFixtureDurationMs: what the
                // decoder reports is what the walk uses, and a rounded value would put every expected
                // figure below slightly out while looking like a real failure.
                const auto fixtureDuration = tracks.begin()->second.duration;
                report.check(fixtureDuration > Duration_t{0}, std::format("the fixtures have a usable duration ({} ms)", fixtureDuration.count()));

                std::vector<TrackId> orderedIds;
                for (const auto &entry : tracks)
                {
                    orderedIds.push_back(entry.second.trackId);
                }

                const auto buildTracks = [&orderedIds, fixtureDuration](size_t count)
                {
                    std::vector<MixTrack> built;
                    for (size_t i = 0; i < count && i < orderedIds.size(); ++i)
                    {
                        MixTrack mixTrack{};
                        mixTrack.trackId = orderedIds[i];
                        mixTrack.orderInMix = static_cast<int>(i);
                        mixTrack.attachFrom = Duration_t{kOverlapMs};
                        mixTrack.attachTo = fixtureDuration;
                        built.push_back(mixTrack);
                    }
                    return built;
                };

                // What the walk should produce: the first track in full, then each further track
                // adding its length less the overlap. Written out here rather than by calling the
                // production walk, so this asserts a number and not that the code agrees with itself.
                const auto expectedLength = [fixtureDuration](size_t count) -> Duration_t
                {
                    if (count == 0)
                    {
                        return Duration_t{0};
                    }
                    return fixtureDuration + Duration_t{static_cast<int64_t>(count - 1) * (fixtureDuration.count() - kOverlapMs)};
                };

                const auto storedLengthOf = [&mixes](MixId mixId)
                {
                    return mixes.getMix(mixId).totalDuration;
                };

                // (a) Creation stores the walk, not the sum, and not what the caller passed.
                MixInfo durationMix{};
                durationMix.name = "SelfTest Duration Mix";
                // Deliberately absurd, and deliberately non-zero so it would have survived the old
                // assertion. If this value reaches the database, the caller is still being trusted.
                durationMix.totalDuration = Duration_t{99'999'999};

                auto twoTracks = buildTracks(2);
                const bool created = mixes.createOrUpdateMix(durationMix, twoTracks);
                report.check(created && durationMix.mixId > 0, "a mix could be created for the duration checks");

                if (created && durationMix.mixId > 0)
                {
                    report.check(storedLengthOf(durationMix.mixId) == expectedLength(2),
                        std::format("creation stores the walked length ({} ms, expected {} ms)",
                            storedLengthOf(durationMix.mixId).count(),
                            expectedLength(2).count()));
                    report.check(durationMix.totalDuration == expectedLength(2),
                        "creation hands the derived length back to the caller instead of keeping the one passed in");

                    // (b) Appending replaces the length. This is the one that regressed: the old code
                    // stored the previous total, so the figure never grew with the mix.
                    auto fourTracks = buildTracks(4);
                    report.check(mixes.createOrUpdateMix(durationMix, fourTracks), "tracks could be appended to the mix");
                    report.check(storedLengthOf(durationMix.mixId) == expectedLength(4),
                        std::format("appending stores the new length, not the previous one ({} ms, expected {} ms)",
                            storedLengthOf(durationMix.mixId).count(),
                            expectedLength(4).count()));

                    // (c) Deleting a track goes through removeTracksFromMix, which writes MixTracks
                    // directly and never touched Mixes.total_length.
                    report.check(mixes.removeTracksFromMix(durationMix.mixId, {orderedIds.back()}), "a track could be removed from the mix");
                    report.check(storedLengthOf(durationMix.mixId) == expectedLength(3),
                        std::format("removing a track updates the stored length ({} ms, expected {} ms)",
                            storedLengthOf(durationMix.mixId).count(),
                            expectedLength(3).count()));
                    report.check(mixes.getMix(durationMix.mixId).numberOfTracks == 3, "removing a track updates the stored count");

                    // (d) A cue edit changes the length without changing the membership. updateMixTrack
                    // is another direct MixTracks write.
                    auto edited = mixes.getMixTracks(durationMix.mixId);
                    report.check(edited.size() == 3, "the mix reads back with three tracks before the cue edit");
                    if (edited.size() == 3)
                    {
                        // Trims the last track, so only the tail of the mix moves.
                        edited.back().cueEnd = Duration_t{-10};
                        report.check(mixes.updateMixTrack(durationMix.mixId, edited.back()), "a cue point could be edited");
                        report.check(storedLengthOf(durationMix.mixId) == expectedLength(3) - Duration_t{10},
                            std::format("a cue edit updates the stored length ({} ms, expected {} ms)",
                                storedLengthOf(durationMix.mixId).count(),
                                (expectedLength(3) - Duration_t{10}).count()));
                        edited.back().cueEnd = Duration_t{0};
                        mixes.updateMixTrack(durationMix.mixId, edited.back());
                    }

                    // (e) Reordering. With three identical tracks the length cannot change, so a check
                    // against it would pass whether or not reordering refreshes anything. The first
                    // track is therefore given a distinct attachTo, which makes the total depend on the
                    // order: the walk excludes the last track's attachTo and the first track's
                    // attachFrom, so moving that track from front to back changes the answer.
                    auto ordered = mixes.getMixTracks(durationMix.mixId);
                    report.check(ordered.size() == 3, "the mix has three tracks before the reorder");
                    if (ordered.size() == 3)
                    {
                        ordered.front().attachTo = fixtureDuration - Duration_t{40};
                        report.check(mixes.updateMixTrack(durationMix.mixId, ordered.front()), "the first track could be given a distinct attach point");

                        // [A B C] with A handing over 40 ms earlier: A at 0, B at D-60, C at 2D-80.
                        const auto beforeReorder = Duration_t{3 * fixtureDuration.count() - 80};
                        report.check(storedLengthOf(durationMix.mixId) == beforeReorder,
                            std::format("the distinct attach point shortened the mix ({} ms, expected {} ms)",
                                storedLengthOf(durationMix.mixId).count(),
                                beforeReorder.count()));

                        // [B C A]: B at 0, C at D-20, A at 2D-40. A's early handover no longer counts,
                        // because nothing follows it.
                        const auto afterReorder = Duration_t{3 * fixtureDuration.count() - 40};
                        report.check(afterReorder != beforeReorder, "the two orders really do have different lengths, so the next check can fail");

                        report.check(mixes.reorderTrackInMix(durationMix.mixId, 0, 2), "a track could be reordered");
                        report.check(storedLengthOf(durationMix.mixId) == afterReorder,
                            std::format("reordering updates the stored length ({} ms, expected {} ms)",
                                storedLengthOf(durationMix.mixId).count(),
                                afterReorder.count()));

                        // Back to uniform, so the checks after this one can keep using expectedLength.
                        auto restored = mixes.getMixTracks(durationMix.mixId);
                        restored.back().attachTo = fixtureDuration;
                        report.check(mixes.updateMixTrack(durationMix.mixId, restored.back()), "the attach point could be restored");
                        report.check(storedLengthOf(durationMix.mixId) == expectedLength(3),
                            std::format("restoring it returns the mix to its uniform length ({} ms, expected {} ms)",
                                storedLengthOf(durationMix.mixId).count(),
                                expectedLength(3).count()));
                    }

                    // (f) A track deleted from the middle of the mix.
                    //
                    // Not the case the review asked to pin down, and it cannot be: MixTracks.track_id
                    // is a foreign key that cascades, so deleting a track takes its mix row with it.
                    // A row referring to a track that cannot be resolved therefore cannot exist while
                    // that key stands - which is why the skip branch in calculateMixDuration, and the
                    // disagreement between it, the playback engine and the exporter over what to do
                    // with such a row, are unreachable today. Zero rows in the real library point at a
                    // missing track. What is reachable, and what this checks, is that a cascade leaves
                    // the stored length describing what remains.
                    const auto beforeDeletion = mixes.getMixTracks(durationMix.mixId);
                    report.check(beforeDeletion.size() == 3, std::format("the mix has three tracks before the deletion (has {})", beforeDeletion.size()));
                    if (beforeDeletion.size() == 3)
                    {
                        report.check(db.removeTracks({beforeDeletion[1].trackId}).isOk(), "the middle track could be deleted from the library");

                        const auto survivors = mixes.getMixTracks(durationMix.mixId);
                        report.check(survivors.size() == 2, std::format("deleting it cascaded its row out of the mix (left {})", survivors.size()));

                        // Read before anything is recomputed. Repairing first and checking afterwards
                        // would pass whether or not the deletion refreshed anything, which is exactly
                        // what production does not do for itself.
                        report.check(storedLengthOf(durationMix.mixId) == expectedLength(2),
                            std::format("deleting a track updates the mixes it cascaded out of ({} ms, expected {} ms)",
                                storedLengthOf(durationMix.mixId).count(),
                                expectedLength(2).count()));
                        report.check(mixes.getMix(durationMix.mixId).numberOfTracks == 2, "the cascade updated the stored count too");

                        // And the recomputation finds nothing to do, which is the same statement made
                        // from the other side: the deletion already left the row correct.
                        MixDurationCheck check;
                        report.check(mixes.recomputeMixDuration(durationMix.mixId, check).isOk(), "the mix can be rechecked after the cascade");
                        report.check(!check.changed, "the recheck finds nothing to correct, because the deletion already did it");
                    }

                    // (g) Rechecking a mix that is already right must not write. That is what makes a
                    // second run of the repair a verification of the first rather than a repeat.
                    MixDurationCheck recheck;
                    report.check(mixes.recomputeMixDuration(durationMix.mixId, recheck).isOk(), "a correct mix can be rechecked");
                    report.check(!recheck.changed, "rechecking a mix that is already correct does not write to it");
                    report.check(recheck.previous.totalLength == recheck.current.totalLength,
                        "an unchanged recheck reports the same length before and after");

                    // (h) The repair actually repairing something.
                    //
                    // Every check above starts from a correct row, so none of them would notice if
                    // recomputeMixDuration never wrote anything at all. This stages the state the whole
                    // one-off pass exists for - a stored summary that disagrees with the rows - and
                    // checks the correction, both reported figures, and what actually landed in the row.
                    //
                    // Staged over the test's own connection because nothing reachable through the
                    // interfaces can write those columns any more. That is the fix; it is also why this
                    // case has to be built from outside.
                    const auto correctLength = storedLengthOf(durationMix.mixId);
                    const auto correctCount = mixes.getMix(durationMix.mixId).numberOfTracks;

                    // Deliberately both wrong, and wrong in the direction the real library was: a length
                    // far longer than the mix, and a count that does not match its rows either.
                    constexpr int64_t kWrongLengthMs = 99'999'999;
                    const bool staged = setMixSummary(databasePath, durationMix.mixId, correctCount + 7, kWrongLengthMs);
                    report.check(staged, "a wrong summary could be staged onto the mix");

                    if (staged)
                    {
                        report.check(storedLengthOf(durationMix.mixId) == Duration_t{kWrongLengthMs}, "the staged summary really is in the row");

                        MixDurationCheck repair;
                        report.check(mixes.recomputeMixDuration(durationMix.mixId, repair).isOk(), "the mix with a wrong summary can be rechecked");
                        report.check(repair.changed, "a mix whose summary disagrees with its rows is corrected");

                        report.check(repair.previous.totalLength == Duration_t{kWrongLengthMs} && repair.previous.trackCount == correctCount + 7,
                            "the report of what was there beforehand matches what was staged");
                        report.check(repair.current.totalLength == correctLength && repair.current.trackCount == correctCount,
                            "the report of what it became matches the walk");

                        // The row itself, not just what the call said about it.
                        report.check(storedLengthOf(durationMix.mixId) == correctLength,
                            std::format("the corrected length reached the database ({} ms, expected {} ms)",
                                storedLengthOf(durationMix.mixId).count(),
                                correctLength.count()));
                        report.check(mixes.getMix(durationMix.mixId).numberOfTracks == correctCount, "the corrected count reached the database too");

                        // And running it again finds nothing to do, which is what makes a second pass a
                        // verification of the first.
                        MixDurationCheck second;
                        report.check(mixes.recomputeMixDuration(durationMix.mixId, second).isOk(), "the repaired mix can be rechecked");
                        report.check(!second.changed, "a second pass over a repaired mix changes nothing");
                    }

                    // (i) A middle row that cannot be decoded.
                    //
                    // The failure this pins down is not the parse itself - it is what the readers do
                    // with it. Returning the rows that came before the bad one hands back a shorter
                    // mix, and the next save rewrites the row set to match: every track after the
                    // corrupt one, permanently gone. So the rule is all rows or none, and every caller
                    // that might write the mix back has to be able to tell those apart from an empty
                    // mix.
                    //
                    // Three rows first. The cascade above left two, and corrupting the last of two
                    // proves less: with a valid row on each side of the bad one, a reader that
                    // published what it had managed to read would hand back exactly one row - a number
                    // that is neither the empty answer nor the right one, so the checks below can tell
                    // all three outcomes apart.
                    auto survivors = mixes.getMixTracks(durationMix.mixId);
                    report.check(survivors.size() == 2, "two rows survived the cascade, to be built back up to three");
                    if (survivors.size() == 2)
                    {
                        // The same track twice is fine - a mix may list one more than once - and it
                        // avoids depending on a fourth track that earlier steps may have deleted.
                        auto three = survivors;
                        three.push_back(survivors.front());

                        // Renumbered from zero, rather than kept as they came back. A cascade does not
                        // renumber, so these two carry orders 0 and 2 - and MixTracks has only an
                        // index on (mix_id, order_in_mix), not a unique constraint, so appending a
                        // third at position 2 is accepted and leaves the mix with no row at position
                        // 1 at all. The corruption below then matches nothing.
                        for (size_t i = 0; i < three.size(); ++i)
                        {
                            three[i].orderInMix = static_cast<int>(i);
                        }
                        report.check(mixes.createOrUpdateMix(durationMix, three), "the mix could be built back up to three rows");

                        const auto rebuilt = mixes.getMixTracks(durationMix.mixId);
                        bool contiguous = rebuilt.size() == 3;
                        for (size_t i = 0; contiguous && i < rebuilt.size(); ++i)
                        {
                            contiguous = rebuilt[i].orderInMix == static_cast<int>(i);
                        }
                        report.check(contiguous, "the rebuilt rows sit at positions 0, 1 and 2, so there is a middle one to corrupt");
                    }

                    const auto beforeCorruption = mixes.getMix(durationMix.mixId);
                    report.check(beforeCorruption.numberOfTracks == 3, std::format("three rows are in place before the corruption (found {})", beforeCorruption.numberOfTracks));

                    // Order 1 of 0, 1, 2: a valid row before it and a valid row after it.
                    const bool corrupted = corruptMixData(databasePath, durationMix.mixId, 1);
                    report.check(corrupted, "the middle row's mix_data could be corrupted");

                    if (corrupted)
                    {
                        std::vector<MixTrack> readBack;
                        const auto readResult = mixes.readMixTracks(durationMix.mixId, readBack);
                        report.check(!readResult.isOk(), "reading a mix with an undecodable row fails rather than succeeding partly");
                        report.check(readBack.empty(),
                            std::format("a failed read hands back nothing, not the row before the bad one (got {})", readBack.size()));
                        report.check(readResult.errorMessage.find("mix_data") != std::string::npos,
                            std::format("the reason names what went wrong (said: '{}')", readResult.errorMessage));

                        // The statusless wrapper is what display-only callers use. It must return an
                        // empty mix, which is obviously wrong, rather than a prefix, which is not.
                        report.check(mixes.getMixTracks(durationMix.mixId).empty(), "the statusless reader returns nothing rather than a prefix");

                        // And the summary is not rewritten from what could be parsed.
                        MixDurationCheck afterCorruption;
                        report.check(!mixes.recomputeMixDuration(durationMix.mixId, afterCorruption).isOk(),
                            "recomputing refuses a mix it cannot fully read");
                        report.check(!afterCorruption.changed, "a refused recomputation writes nothing");

                        const auto afterMix = mixes.getMix(durationMix.mixId);
                        report.check(afterMix.totalDuration == beforeCorruption.totalDuration && afterMix.numberOfTracks == beforeCorruption.numberOfTracks,
                            "the stored summary is left exactly as it was");
                    }

                    mixes.removeMix(durationMix.mixId);
                }
            }

            // --- 8. A forced rescan refreshes rows instead of colliding with them. ---
            //
            // The forced path used to build a TrackInfo from the file, leave its track id unset, and
            // hand it to saveTrackInfo - which decides insert or update on that id. So every file it
            // was asked to refresh took the INSERT branch and hit UNIQUE(folder_id, filename). A forced
            // rescan did nothing at all, once per file, and reported success.
            //
            // Setting the id would have been the smaller fix and the wrong one: saveTrackInfo's UPDATE
            // writes every column, and a TrackInfo built from a file carries defaults for everything
            // the file cannot answer for. The checks below are as much about what a rescan must not
            // touch as about what it must.
            {
                // A baseline of its own, taken now rather than reusing step 1's.
                //
                // Section 7 deleted a track from the library whose file is still sitting in the tree, so
                // the next scan legitimately inserts it again under a new id. Comparing against the ids
                // step 1 recorded would fail on that alone and say nothing about the forced path. One
                // plain scan settles the library against the tree first; everything below is measured
                // from what that leaves.
                report.check(runScan({rootFolderId}, false, report, "settle before the forced rescan"), "the settling scan reports success");

                const auto baselineCount = static_cast<int>(trackRowsUnder(db, rootFolderId).size());
                const auto baselineIds = idsOf(tracksUnder(db, rootFolderId));
                report.check(baselineCount > 0 && static_cast<int>(baselineIds.size()) == baselineCount,
                    std::format("the library and the tree agree before the forced rescan ({} tracks)", baselineCount));

                const auto victimId = baselineIds.begin()->second;

                // Analysis and library history, staged so a rescan has something to flatten. Neither is
                // derivable from the file, which is what makes them the test.
                report.check(db.updateTrackBpm(victimId, AudioMetadata{128.0f}).isOk(), "a BPM could be staged onto a track");

                const auto before = db.getTrackById(victimId);
                report.check(before.has_value() && before->bpm.has_value() && before->bpm.value() > 0, "the staged BPM really is in the row");

                // Compared against itself afterwards rather than against a literal: the column is
                // normalised on the way in, and this is a test of what a rescan preserves, not of the
                // normalisation constant.
                const auto bpmBefore = (before.has_value() && before->bpm.has_value()) ? before->bpm.value() : 0;
                const auto dateAddedBefore = before.has_value() ? before->date_added : Timestamp_t{};

                // A title to lose, staged before the rescan. The fixtures carry no tags at all, so
                // without this every check about tags below would be comparing empty to empty.
                const auto stageTitle = [&db, victimId](const std::string &title)
                {
                    const auto row = db.getTrackById(victimId);
                    if (!row.has_value())
                    {
                        return false;
                    }

                    auto staged = row.value();
                    staged.title = title;
                    return db.updateScannedTrackData(staged, ScannedFields::Tags).isOk();
                };

                report.check(stageTitle("SelfTest Title Before Rescan"), "a title could be staged onto the track");

                report.check(runScan({rootFolderId}, false, report, "forced rescan", true), "a forced rescan reports success");

                const auto rescanRows = trackRowsUnder(db, rootFolderId);
                report.check(static_cast<int>(rescanRows.size()) == baselineCount,
                    std::format("a forced rescan inserted nothing - still {} rows (found {})", baselineCount, rescanRows.size()));
                report.check(idsOf(tracksUnder(db, rootFolderId)) == baselineIds, "and every track kept its id through the forced rescan");

                const auto after = db.getTrackById(victimId);
                report.check(after.has_value(), "the rescanned track can still be read back");
                if (after.has_value())
                {
                    report.check(after->bpm.has_value() && after->bpm.value() == bpmBefore,
                        "a forced rescan leaves the BPM alone - the file cannot answer for it");
                    report.check(after->date_added == dateAddedBefore,
                        "and leaves date_added alone - that is the library's history, not the file's");
                    report.check(after->duration > Duration_t{0}, "while the columns a scan does own are filled in");
                    report.check(!after->is_missing, "and the track is not missing");

                    // The other half of the rule. This file reads perfectly well and genuinely has no
                    // tags, so its empty tag is an answer and the staged title has to go. Without this,
                    // "never write an empty tag" would be indistinguishable from the correct rule, and
                    // clearing a title in a tag editor would stop working.
                    report.check(after->title.empty(),
                        std::format("a readable file with no tags clears the title - an empty tag is an answer (title is now '{}')", after->title));
                }

                // A file that cannot be read is not a file that says everything is blank.
                //
                // Id3TagScanner returns false for a file it cannot open, having filled in nothing - so
                // the TrackInfo carries a zero duration, an empty title and no audio properties. Writing
                // those over an existing row erases what the library knew because one read failed. The
                // file is truncated rather than deleted so that the scan still finds it: this is the
                // unreadable case, not the missing case, and the two take different paths.
                const auto durationBefore = after.has_value() ? after->duration : Duration_t{0};
                const auto victimPath = albumPath / (after.has_value() ? after->filename : std::string{});
                report.check(durationBefore > Duration_t{0}, "the track about to be truncated has a duration to lose");

                // And a title to lose as well. TagLib returns a non-null, empty tag object for a
                // malformed file exactly as it does for a real file with no tags, so without a
                // non-empty title staged here the erasure would look identical to a correct no-op.
                const std::string titleBefore{"SelfTest Title Before Truncation"};
                report.check(stageTitle(titleBefore), "a title could be staged before the file is truncated");

                {
                    std::ofstream truncate{victimPath, std::ios::binary | std::ios::trunc};
                    report.check(truncate.good(), std::format("{} could be truncated to nothing", pathToString(victimPath)));
                }

                report.check(runScan({rootFolderId}, false, report, "forced rescan over an unreadable file", true),
                    "a forced rescan over an unreadable file reports success");

                const auto afterTruncation = db.getTrackById(victimId);
                report.check(afterTruncation.has_value(), "the unreadable track still has its row");
                if (afterTruncation.has_value())
                {
                    report.check(afterTruncation->duration == durationBefore, "an unreadable file does not blank the duration the library already had");
                    report.check(afterTruncation->bpm.has_value() && afterTruncation->bpm.value() == bpmBefore, "nor the BPM");
                    report.check(afterTruncation->title == titleBefore,
                        std::format("nor the title - an empty tag object from a file that would not decode is not an answer (title is now '{}')",
                            afterTruncation->title));
                    report.check(afterTruncation->filesize_bytes == 0, "while the size the filesystem reports is written, because that much was read");
                }
            }

            // --- 9. A file that moved keeps its track id, and the mixes that use it. ---
            //
            // Moving a file used to be an insert and a flagging: a new row for the file in its new
            // folder, the old row marked missing. Every mix, working set and album entry stayed pointing
            // at the row that was now missing, so reorganising a folder outside the app broke every mix
            // that used anything in it - with the files sitting right there on disk.
            //
            // The decision cannot be made during the walk, because "the old file is gone" is not known
            // until every folder has been visited. What that buys is the second half of this section: a
            // file that was copied rather than moved must not take the original's identity.
            {
                const auto movedRoot = workRoot / "moved";
                const auto copyRoot = workRoot / "copied";
                if (makeDirectory(movedRoot) && makeDirectory(copyRoot))
                {
                    // Again its own baseline, and its own choice of victim: which of the fixture files
                    // still has a row depends on what the sections above deleted, so the file to move is
                    // picked from what is actually in the library now.
                    const auto beforeMove = tracksUnder(db, rootFolderId);
                    const auto baselineCount = static_cast<int>(trackRowsUnder(db, rootFolderId).size());
                    const auto baselineIds = idsOf(beforeMove);

                    // What the mix lists now, not what it was built with - section 7's cascade took a
                    // row out of it, and the point here is that the move takes none.
                    const auto mixTracksBeforeMove = theTrackLibrary.getMixManager().getMixTracks(mixInfo.mixId);
                    const auto mixBeforeMove = mixTracksBeforeMove.size();

                    // A track the mix actually uses, or the mix check below would hold however badly the
                    // move went. Not just any surviving row: the settling scan above re-inserted the file
                    // section 7 deleted, and that new row is in no mix at all.
                    std::string movedName;
                    TrackId movedTrackId{-1};
                    FolderId albumFolderId{-1};
                    for (const auto &entry : beforeMove)
                    {
                        const auto inTheMix = std::any_of(mixTracksBeforeMove.begin(),
                            mixTracksBeforeMove.end(),
                            [&entry](const MixTrack &mixTrack)
                            {
                                return mixTrack.trackId == entry.second.trackId;
                            });
                        if (inTheMix && entry.second.folderId == db.getFolderDatabase().findOrCreateFolderByPath(albumPath))
                        {
                            movedName = entry.second.filename;
                            movedTrackId = entry.second.trackId;
                            albumFolderId = entry.second.folderId;
                            break;
                        }
                    }
                    const auto movedTo = movedRoot / movedName;

                    report.check(movedTrackId > 0, std::format("the track about to be moved ('{}') is in the library and in the mix", movedName));

                    std::error_code moveEc;
                    if (movedTrackId > 0)
                    {
                        std::filesystem::rename(albumPath / movedName, movedTo, moveEc);
                        report.check(!moveEc, std::format("the file could be moved into {}", pathToString(movedRoot)));
                    }

                    if (!moveEc && movedTrackId > 0)
                    {
                        report.check(runScan({rootFolderId}, false, report, "after a file moved"), "the scan after the move reports success");

                        // Rows for the count, because a duplicate insert would put the same filename in
                        // a second folder - which is precisely what a map keyed by filename hides. The
                        // keyed view is still what the id comparison needs.
                        const auto moveRows = trackRowsUnder(db, rootFolderId);
                        report.check(static_cast<int>(moveRows.size()) == baselineCount,
                            std::format("no duplicate row was inserted for the moved file - still {} (found {})", baselineCount, moveRows.size()));
                        report.check(countMissing(moveRows) == 0, "and nothing is flagged missing, because nothing is");
                        report.check(idsOf(tracksUnder(db, rootFolderId)) == baselineIds, "every track id survived the move, the moved one included");

                        // The mix is the reason any of this matters. Matching ids are not enough on
                        // their own: MixTracks cascades on track deletion, so a delete-and-reinsert
                        // would leave the mix short while the id set still looked plausible.
                        const auto mixAfterMove = theTrackLibrary.getMixManager().getMixTracks(mixInfo.mixId).size();
                        report.check(mixAfterMove == mixBeforeMove,
                            std::format("the mix still lists the same {} tracks after the move (lists {})", mixBeforeMove, mixAfterMove));

                        const auto moved = db.getTrackById(movedTrackId);
                        report.check(moved.has_value() && moved->folderId != albumFolderId,
                            "the moved track's row now names the folder the file is actually in");
                    }

                    // A copy is not a move. Same name, same size, but the original never went anywhere,
                    // so the match must be refused and a new row inserted. Getting this wrong hands the
                    // original's mix references to the copy and leaves the original looking new.
                    std::error_code copyEc;
                    if (!moveEc && movedTrackId > 0)
                    {
                        std::filesystem::copy_file(movedTo, copyRoot / movedName, copyEc);
                        report.check(!copyEc, "a second file with the same name and size could be made");
                    }

                    if (!copyEc && !moveEc && movedTrackId > 0)
                    {
                        report.check(runScan({rootFolderId}, false, report, "after a file was copied"), "the scan after the copy reports success");

                        // Unkeyed, because the copy and the original share a filename and a keyed map
                        // would show one entry whether the insert happened or not.
                        const auto afterCopy = trackRowsUnder(db, rootFolderId);
                        report.check(static_cast<int>(afterCopy.size()) == baselineCount + 1,
                            std::format("the copy was inserted as its own track ({} rows, expected {})", afterCopy.size(), baselineCount + 1));

                        const auto stillThere = db.getTrackById(movedTrackId);
                        report.check(stillThere.has_value() && stillThere->folderId == db.getFolderDatabase().findOrCreateFolderByPath(movedRoot),
                            "and the original kept its own row rather than being re-identified as the copy");
                        report.check(countMissing(afterCopy) == 0, "with nothing flagged missing on either side of it");
                    }
                }
            }

            // --- 10. Two shapes that look like a move and are not. ---
            //
            // The rule is one file, one row, and that row's file is gone. Each half below breaks one of
            // those and must be refused: guessing here attaches a track's history and every mix that
            // uses it to an arbitrary file, and nothing afterwards says it happened.
            {
                const auto ambiguousRoot = workRoot / "ambiguous";
                const auto twinRootA = ambiguousRoot / "a";
                const auto twinRootB = ambiguousRoot / "b";
                const auto copyRoot = workRoot / "copied"; // section 9's, named again rather than shared

                if (makeDirectory(twinRootA) && makeDirectory(twinRootB))
                {
                    // (a) A row that vanished, and another row of the same name and size that did not.
                    //
                    // Section 9 left two rows sharing a name and size: the moved file and the copy. Move
                    // the copy on, and its old row is gone while the other is still on disk - so the
                    // name and size identify nothing, and the fact that one of them vanished does not
                    // make the survivor's twin this file.
                    const auto rowsBefore = static_cast<int>(trackRowsUnder(db, rootFolderId).size());
                    const auto copyFolderId = db.getFolderDatabase().findOrCreateFolderByPath(copyRoot);

                    TrackId copyTrackId{-1};
                    std::string twinName;
                    for (const auto &track : trackRowsUnder(db, rootFolderId))
                    {
                        if (track.folderId == copyFolderId)
                        {
                            copyTrackId = track.trackId;
                            twinName = track.filename;
                        }
                    }
                    report.check(copyTrackId > 0, "section 9's copy has a row of its own to move");

                    std::error_code twinEc;
                    if (copyTrackId > 0)
                    {
                        std::filesystem::rename(copyRoot / twinName, twinRootA / twinName, twinEc);
                        report.check(!twinEc, "the copy could be moved on again");
                    }

                    if (copyTrackId > 0 && !twinEc)
                    {
                        report.check(runScan({rootFolderId}, false, report, "a vanished row with a live twin"),
                            "the scan reports success with an ambiguous match on offer");

                        const auto rowsAfter = trackRowsUnder(db, rootFolderId);
                        report.check(static_cast<int>(rowsAfter.size()) == rowsBefore + 1,
                            std::format("the file was inserted as a new track rather than matched ({} rows, expected {})",
                                rowsAfter.size(),
                                rowsBefore + 1));

                        const auto orphan = db.getTrackById(copyTrackId);
                        report.check(orphan.has_value() && orphan->is_missing,
                            "and the row whose file went away is flagged missing, not quietly handed to the new file");
                        report.check(orphan.has_value() && orphan->folderId == copyFolderId,
                            "the flagged row still names the folder it was in");
                    }

                    // (b) One vanished row, two new files that both match it.
                    //
                    // Whichever the directory walk returned first would otherwise take the identity,
                    // which is an accident rather than a decision.
                    const auto rowsBeforeTwins = static_cast<int>(trackRowsUnder(db, rootFolderId).size());
                    const auto albumFolderId = db.getFolderDatabase().findOrCreateFolderByPath(albumPath);

                    TrackId twinSourceId{-1};
                    std::string twinSourceName;
                    for (const auto &track : trackRowsUnder(db, rootFolderId))
                    {
                        if (track.folderId == albumFolderId && !track.is_missing && track.filesize_bytes > 0)
                        {
                            twinSourceId = track.trackId;
                            twinSourceName = track.filename;
                        }
                    }
                    report.check(twinSourceId > 0, "a readable track is still in the album folder to make twins of");

                    if (twinSourceId > 0)
                    {
                        std::error_code copyEcA;
                        std::error_code copyEcB;
                        std::error_code removeEc;
                        std::filesystem::copy_file(albumPath / twinSourceName, twinRootA / twinSourceName, copyEcA);
                        std::filesystem::copy_file(albumPath / twinSourceName, twinRootB / twinSourceName, copyEcB);
                        std::filesystem::remove(albumPath / twinSourceName, removeEc);
                        report.check(!copyEcA && !copyEcB && !removeEc, "one file could be turned into two identical files elsewhere");

                        if (!copyEcA && !copyEcB && !removeEc)
                        {
                            report.check(runScan({rootFolderId}, false, report, "two new files matching one vanished row"),
                                "the scan reports success with two files competing for one row");

                            const auto rowsAfterTwins = trackRowsUnder(db, rootFolderId);
                            report.check(static_cast<int>(rowsAfterTwins.size()) == rowsBeforeTwins + 2,
                                std::format("both files were inserted as new tracks ({} rows, expected {})",
                                    rowsAfterTwins.size(),
                                    rowsBeforeTwins + 2));

                            const auto contested = db.getTrackById(twinSourceId);
                            report.check(contested.has_value() && contested->is_missing,
                                "and the row they were competing for is flagged missing rather than given to one of them");
                            report.check(contested.has_value() && contested->folderId == albumFolderId,
                                "it did not follow either file out of the album folder");
                        }
                    }
                }
            }

            // --- 11. The write mask, one field group at a time. ---
            //
            // A read can half succeed: TagLib hands back a tag object for plenty of files whose audio
            // properties it cannot work out, and the reverse happens too. A single "did the read work"
            // answer forces a choice between throwing away tags that were read and writing a zero
            // duration over a real one, and the second is what a coarse flag would have done.
            //
            // Tested here rather than through a crafted file, because a file that gives up its tags and
            // defeats the property reader is not something a test can reliably construct - while the
            // write mask is exactly where the damage would be done.
            {
                const auto subject = trackRowsUnder(db, rootFolderId);
                report.check(!subject.empty(), "there is a track to write masked updates over");

                if (!subject.empty())
                {
                    const auto original = subject.front();

                    // Everything a scan establishes, deliberately different from what is stored, so
                    // either half writing when it should not is visible.
                    TrackInfo scanned{};
                    scanned.trackId = original.trackId;
                    scanned.folderId = original.folderId;
                    scanned.filename = original.filename;
                    scanned.filesize_bytes = original.filesize_bytes;
                    scanned.last_scanned = std::chrono::system_clock::now();
                    scanned.title = "SelfTest Masked Title";
                    scanned.artist_name = "SelfTest Masked Artist";
                    scanned.duration = Duration_t{1234};
                    scanned.samplerate = 12345;

                    report.check(original.duration != scanned.duration && original.title != scanned.title,
                        "the stored row and the scanned one disagree about both halves, so either can be told apart");

                    // (a) Tags only: the titles move, the audio properties do not.
                    report.check(db.updateScannedTrackData(scanned, ScannedFields::Tags).isOk(), "a tags-only update is accepted");

                    const auto afterTags = db.getTrackById(original.trackId);
                    report.check(afterTags.has_value() && afterTags->title == scanned.title, "a tags-only update writes the title");
                    report.check(afterTags.has_value() && afterTags->duration == original.duration,
                        "and leaves the duration alone - the property read is what failed, and it says nothing");
                    report.check(afterTags.has_value() && afterTags->samplerate == original.samplerate, "nor does it touch the samplerate");

                    // (b) Audio properties only: the reverse.
                    TrackInfo propertiesOnly{scanned};
                    propertiesOnly.title.clear();
                    propertiesOnly.artist_name.clear();
                    report.check(db.updateScannedTrackData(propertiesOnly, ScannedFields::AudioProperties).isOk(),
                        "an audio-properties-only update is accepted");

                    const auto afterProperties = db.getTrackById(original.trackId);
                    report.check(afterProperties.has_value() && afterProperties->duration == scanned.duration,
                        "an audio-properties-only update writes the duration");
                    report.check(afterProperties.has_value() && afterProperties->samplerate == scanned.samplerate, "and the samplerate");
                    report.check(afterProperties.has_value() && afterProperties->title == scanned.title,
                        "and leaves the title that was read earlier alone, rather than blanking it");

                    // (c) Nothing at all still moves the row to where the file is.
                    TrackInfo locationOnly{propertiesOnly};
                    locationOnly.duration = Duration_t{0};
                    locationOnly.samplerate = 0;
                    locationOnly.filesize_bytes = original.filesize_bytes + 1;
                    report.check(db.updateScannedTrackData(locationOnly, ScannedFields::None).isOk(), "an update establishing nothing is still accepted");

                    const auto afterNothing = db.getTrackById(original.trackId);
                    report.check(afterNothing.has_value() && afterNothing->filesize_bytes == original.filesize_bytes + 1,
                        "it writes what the filesystem said, because that much was never in doubt");
                    report.check(afterNothing.has_value() && afterNothing->duration == scanned.duration && afterNothing->title == scanned.title,
                        "and writes none of the metadata it did not read");
                }
            }

            // --- 12. A root nobody could look at is not evidence that its files are gone. ---
            //
            // A leftover only means "the walk did not find it", and the walk finds nothing under a root
            // it could not resolve or could not reach. An unplugged drive therefore produces a library
            // full of rows that look exactly like deleted files - and if a copy of one of them turns up
            // under a root that *was* walked, matching on that would hand the track, and every mix that
            // uses it, to the copy while the original sits there on a disk that is merely unplugged.
            //
            // A deleted *folder* under a healthy root is a different thing entirely, and is the ordinary
            // case section 9 covers. This is about the root.
            {
                const auto secondRoot = selfTestRoot / "second-root";
                const auto strandedName = std::string{"stranded.wav"};

                std::error_code secondEc;
                std::filesystem::remove_all(secondRoot, secondEc);
                if (makeDirectory(secondRoot) && writeSilentWav(secondRoot / strandedName, static_cast<uint32_t>(44100 * kFixtureDurationMs / 1000)))
                {
                    const auto addedRoot = db.getLibraryRootManager().addRoot(pathToString(secondRoot));
                    const auto secondRootFolderId = db.getFolderDatabase().findOrCreateFolderByPath(secondRoot);
                    report.check(addedRoot.has_value() && secondRootFolderId > 0, "a second library root could be added");

                    if (addedRoot.has_value() && secondRootFolderId > 0)
                    {
                        report.check(runScan({rootFolderId, secondRootFolderId}, false, report, "two roots, both present"),
                            "the scan across both roots reports success");

                        TrackId strandedId{-1};
                        for (const auto &track : trackRowsUnder(db, secondRootFolderId))
                        {
                            if (track.filename == strandedName)
                            {
                                strandedId = track.trackId;
                            }
                        }
                        report.check(strandedId > 0, "the file under the second root has a row of its own");

                        // The root goes away entirely - the disconnected-drive shape - and an identical
                        // file turns up under the root that is still there.
                        const auto arrivedRoot = workRoot / "arrived";
                        std::error_code setupEc;
                        if (makeDirectory(arrivedRoot))
                        {
                            std::filesystem::copy_file(secondRoot / strandedName, arrivedRoot / strandedName, setupEc);
                        }
                        std::error_code removeEc;
                        std::filesystem::remove_all(secondRoot, removeEc);
                        report.check(!setupEc && !removeEc, "the second root could be taken away with a copy of its file left elsewhere");

                        if (strandedId > 0 && !setupEc && !removeEc)
                        {
                            const auto rowsBefore = static_cast<int>(trackRowsUnder(db, rootFolderId).size());

                            report.check(runScan({rootFolderId, secondRootFolderId}, false, report, "one root gone, its file apparently elsewhere"),
                                "the scan reports success with a root it could not look at");

                            const auto rowsAfter = trackRowsUnder(db, rootFolderId);
                            report.check(static_cast<int>(rowsAfter.size()) == rowsBefore + 1,
                                std::format("the file under the healthy root was inserted as a new track ({} rows, expected {})",
                                    rowsAfter.size(),
                                    rowsBefore + 1));

                            const auto stranded = db.getTrackById(strandedId);
                            report.check(stranded.has_value(), "the row under the unreachable root still exists");
                            report.check(stranded.has_value() && stranded->folderId == secondRootFolderId,
                                "and still names the folder it was in - it was not handed to the copy");
                        }
                    }

                    if (addedRoot.has_value())
                    {
                        db.getLibraryRootManager().removeRoot(addedRoot->id);
                    }
                }
            }

            writeResultsFile(resultsPath, "jucyaudio scan self test", report);
            spdlog::info("[SelfTest] Finished with {} failure(s). Results: {}", report.failures(), pathToString(resultsPath));
            return report.failures() == 0 ? 0 : 1;
        }

        int runMixRecoverySelfTest(const std::filesystem::path &selfTestRoot, const std::filesystem::path &databasePath)
        {
            Report report;
            // Its own library, separate from the scan suite's: this one deletes tracks and a mix as part
            // of what it asserts, and the two must not be able to disturb each other.
            const auto workRoot = selfTestRoot / "recovery-library";
            const auto exportPath = selfTestRoot / "recovery-export.wav";
            const auto resultsPath = selfTestRoot / "mixrecovery-results.txt";

            spdlog::info("[SelfTest] Starting mix recovery self test. Root: {}", pathToString(selfTestRoot));

            std::error_code ec;
            std::filesystem::remove_all(workRoot, ec);
            if (ec)
            {
                report.abort(std::format("Could not clear {}: {}", pathToString(workRoot), ec.message()));
                writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
                return 1;
            }
            std::filesystem::remove(exportPath, ec);

            const auto albumPath = workRoot / kAlbumFolder;
            std::filesystem::create_directories(albumPath, ec);
            if (ec)
            {
                report.abort(std::format("Could not create {}: {}", pathToString(albumPath), ec.message()));
                writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
                return 1;
            }

            for (int i = 1; i <= kTrackCount; ++i)
            {
                const auto file = albumPath / std::format("rec{:02}.wav", i);
                if (!writeSilentWav(file, static_cast<uint32_t>(44100 * kFixtureDurationMs / 1000)))
                {
                    report.abort(std::format("Could not write the fixture {}", pathToString(file)));
                    writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
                    return 1;
                }
            }

            auto &db = theTrackLibrary.getTrackDatabase();
            auto &mixManager = theTrackLibrary.getMixManager();

            if (!db.getLibraryRootManager().addRoot(pathToString(workRoot)).has_value())
            {
                report.abort("Could not add the recovery scratch library as a library root.");
                writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
                return 1;
            }

            const auto rootFolderId = db.getFolderDatabase().findOrCreateFolderByPath(workRoot);
            if (rootFolderId <= 0 || !runScan({rootFolderId}, false, report, "recovery fixture discovery"))
            {
                report.abort("Could not discover the recovery scratch library.");
                writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
                return 1;
            }

            const auto tracks = tracksUnder(db, rootFolderId);
            if (static_cast<int>(tracks.size()) != kTrackCount)
            {
                report.abort(std::format("Expected {} fixture tracks, found {}.", kTrackCount, tracks.size()));
                writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
                return 1;
            }

            // --- 1. A mix with settings worth losing ---

            // Non-default cue points and gain, so mix_data is something specific rather than an empty
            // shell. A verbatim-copy assertion against defaults would pass even if the copy were wrong.
            std::vector<MixTrack> mixTracks;
            std::vector<TrackId> orderedTrackIds;
            for (const auto &entry : tracks)
            {
                MixTrack mixTrack{};
                mixTrack.trackId = entry.second.trackId;
                mixTrack.orderInMix = static_cast<int>(mixTracks.size());
                mixTrack.cueStart = Duration_t{10 * (mixTrack.orderInMix + 1)};
                mixTrack.cueEnd = Duration_t{kFixtureDurationMs};
                mixTrack.gainAdjustment = 0.5f + (0.1f * static_cast<float>(mixTrack.orderInMix));
                orderedTrackIds.push_back(mixTrack.trackId);
                mixTracks.push_back(mixTrack);
            }

            MixInfo mixInfo{};
            mixInfo.name = "SelfTest Recovery Mix";
            mixInfo.totalDuration = Duration_t{kTrackCount * kFixtureDurationMs};
            if (!mixManager.createOrUpdateMix(mixInfo, mixTracks) || mixInfo.mixId <= 0)
            {
                report.abort("Could not create the recovery test mix.");
                writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
                return 1;
            }
            report.note(std::format("created mix {} with {} tracks", mixInfo.mixId, mixTracks.size()));

            const auto liveMixTracks = mixManager.getMixTracks(mixInfo.mixId);

            // --- 2. Export it for real, and check nothing complained ---

            audio::ActiveExportSettings settings{};
            settings.outputPath = exportPath;

            const audio::MixExporter exporter{};
            const auto exportResult = exporter.exportMixToFile(mixInfo.mixId, settings, nullptr);

            report.check(exportResult.success, "exporting the mix to WAV succeeds");
            report.check(exportResult.recoveryWarning.empty(),
                std::format("the export reports no recovery warning (got: '{}')", exportResult.recoveryWarning));
            report.check(std::filesystem::exists(exportPath, ec), "the WAV file was written");

            // --- 3. What was recorded matches what was exported ---

            std::vector<MixRecoveryEntry> recorded;
            report.check(mixManager.getRecoveryData(mixInfo.mixId, recorded).isOk(), "recovery data reads back without error");
            report.check(static_cast<int>(recorded.size()) == kTrackCount,
                std::format("{} recovery rows were written (found {})", kTrackCount, recorded.size()));

            // Asserted in its own right rather than used as a silent gate. It is a separate prerequisite,
            // and letting it skip the position and id checks would turn one failure into no failures.
            report.check(static_cast<int>(liveMixTracks.size()) == kTrackCount,
                std::format("the live mix still lists {} tracks (found {})", kTrackCount, liveMixTracks.size()));

            if (static_cast<int>(recorded.size()) == kTrackCount)
            {
                bool positionsOk = true;
                bool idsOk = true;
                for (size_t i = 0; i < recorded.size(); ++i)
                {
                    positionsOk = positionsOk && recorded[i].orderInMix == static_cast<int>(i) && recorded[i].mixId == mixInfo.mixId;
                    idsOk = idsOk && recorded[i].trackId == orderedTrackIds[i];
                }
                report.check(positionsOk, "recovery rows carry contiguous positions and the right mix id");
                report.check(idsOk, "recovery rows carry the same track ids, in the same order, as the mix");

                // Stored with the record rather than fetched from the live mix when needed. A playlist
                // whose tracks come from the record and whose length comes from the current mix would be
                // describing two different mixes at once.
                const bool durationOk = std::all_of(recorded.begin(),
                    recorded.end(),
                    [&mixInfo](const MixRecoveryEntry &entry)
                    {
                        return entry.mixTotalDuration == mixInfo.totalDuration;
                    });
                report.check(durationOk, "every recovery row remembers how long the whole mix was");
            }

            if (recorded.size() == liveMixTracks.size())
            {
                bool settingsOk = true;
                for (size_t i = 0; i < recorded.size(); ++i)
                {
                    MixTrack fromRecord{};
                    if (!recorded[i].mixData.empty())
                    {
                        try
                        {
                            json::parse(recorded[i].mixData).get_to(fromRecord);
                        }
                        catch (const std::exception &e)
                        {
                            report.note(std::format("recovery row {} has unparseable mix_data: {}", i, e.what()));
                            settingsOk = false;
                            continue;
                        }
                    }

                    const auto &live = liveMixTracks[i];
                    settingsOk = settingsOk && fromRecord.cueStart == live.cueStart && fromRecord.cueEnd == live.cueEnd &&
                                 fromRecord.attachFrom == live.attachFrom && fromRecord.attachTo == live.attachTo &&
                                 fromRecord.gainAdjustment == live.gainAdjustment && fromRecord.envelopePoints == live.envelopePoints;
                }
                report.check(settingsOk, "recorded mix_data round-trips to the same cue points, attach points, gain and envelope");
            }

            // --- 3b. mix_data is stored verbatim, not re-serialised ---

            {
                // An unknown field is staged directly into MixTracks, then the mix is exported again
                // through the public API. A capture that parsed and re-serialised on the way in would
                // drop the field, because to_json does not know about it; a verbatim copy keeps it.
                const auto injected = injectUnknownFieldIntoMixData(databasePath, mixInfo.mixId, 0);
                report.check(!injected.empty(), "an unknown JSON field could be staged into the live mix_data");

                if (!injected.empty())
                {
                    const auto reExport = exporter.exportMixToFile(mixInfo.mixId, settings, nullptr);
                    report.check(reExport.success && reExport.recoveryWarning.empty(),
                        std::format("re-exporting after the injection succeeds and records cleanly (warning: '{}')", reExport.recoveryWarning));

                    std::vector<MixRecoveryEntry> afterInjection;
                    std::ignore = mixManager.getRecoveryData(mixInfo.mixId, afterInjection);
                    report.check(!afterInjection.empty() && afterInjection.front().mixData == injected,
                        "mix_data was stored byte for byte, keeping a field the parser does not know");
                }
            }

            // Re-read: the record now describes the injected state, and later checks compare against it.
            recorded.clear();
            std::ignore = mixManager.getRecoveryData(mixInfo.mixId, recorded);

            // --- 3c. The companion m3u sits beside the audio and says what it should ---

            {
                const auto companionPath = audio::companionM3UPathFor(exportPath);

                report.check(std::filesystem::exists(companionPath, ec), "a companion m3u was written beside the audio file");
                // A stray temporary next to the real file looks like a half-written recovery artefact,
                // which is worse than none because someone would trust it.
                report.check(strayTempCount(companionPath.parent_path()) == 0, "no temporary was left behind");

                // Scoped so the handle is closed before the re-export below. Windows ReplaceFile refuses
                // to replace a file that anyone still has open, so a reader left dangling here makes the
                // production code look broken when it is behaving correctly.
                std::string text;
                {
                    std::ifstream in{companionPath, std::ios::binary};
                    text.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
                }

                report.check(text.starts_with("#EXTM3U\n"), "the companion starts with the standard header");
                for (const auto *tag : {"#EXTMIX:", "#EXTMIXDURATION:", "#EXTINF:", "#JAALBUM:", "#JASTART:", "#JADURATION:", "#JASIZE:", "#JATRACKID:"})
                {
                    report.check(text.find(tag) != std::string::npos, std::format("the companion carries {}", tag));
                }

                // Presence is not enough. The application sets a global locale with thousands
                // separators, so a value that goes out through operator<< instead of std::format comes
                // out as "20,757" - present, well-formed to the eye, and parsed as 20 by anything
                // reading it as a number. Every field below is a bare integer by definition, so the
                // whole value up to the newline must be digits.
                for (const auto *tag : {"#EXTMIXDURATION:", "#JASTART:", "#JADURATION:", "#JASIZE:", "#JATRACKID:"})
                {
                    const std::string tagText{tag};
                    bool allNumeric = true;
                    std::string offender;
                    for (size_t at = text.find(tagText); at != std::string::npos; at = text.find(tagText, at + 1))
                    {
                        const auto valueAt = at + tagText.size();
                        const auto lineEnd = text.find('\n', valueAt);
                        auto value = text.substr(valueAt, lineEnd - valueAt);

                        // #JASTART alone may be negative, and one mix in the library is: its first
                        // track cues 482 ms before the file begins, so its audible content starts
                        // before the mix does. A sign is part of the number here; a comma is not.
                        if (tagText == "#JASTART:" && value.starts_with('-'))
                        {
                            value.erase(0, 1);
                        }

                        if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
                        {
                            allNumeric = false;
                            offender = value;
                            break;
                        }
                    }
                    report.check(allNumeric, std::format("every {} value is digits only (found \"{}\")", tag, offender));
                }

                // One #EXTINF per track, so nothing was dropped or duplicated.
                size_t extinfCount = 0;
                for (size_t at = text.find("#EXTINF:"); at != std::string::npos; at = text.find("#EXTINF:", at + 1))
                {
                    ++extinfCount;
                }
                report.check(extinfCount == static_cast<size_t>(kTrackCount),
                    std::format("the companion lists {} tracks (found {})", kTrackCount, extinfCount));

                // Written binary, so the bytes are the bytes: no CRLF translation on the way out.
                report.check(text.find("\r\n") == std::string::npos, "the companion has no CRLF - it was written as raw bytes");

                // Re-export replaces it rather than appending to or corrupting it. The result is checked
                // rather than discarded: a failed export would leave the previous file untouched, so the
                // size comparison below would pass while proving nothing.
                const auto sizeBefore = std::filesystem::file_size(companionPath, ec);
                const auto replaceExport = exporter.exportMixToFile(mixInfo.mixId, settings, nullptr);
                report.check(replaceExport.success && replaceExport.recoveryWarning.empty(),
                    std::format("the replacement re-export succeeds and records cleanly (warning: '{}')", replaceExport.recoveryWarning));

                // Only meaningful because the export above was asserted to have succeeded: a failed
                // replacement leaves the previous file untouched, so this comparison would pass while
                // proving the opposite of what it claims.
                const auto sizeAfter = std::filesystem::file_size(companionPath, ec);
                report.check(sizeBefore == sizeAfter, "re-exporting replaces the companion rather than growing it");
                report.check(strayTempCount(companionPath.parent_path()) == 0, "re-exporting leaves no temporary behind either");
            }

            // That export captured again, so the rows carry a fresh capturedAt. Everything below compares
            // against `recorded` to prove nothing changed it, so it has to describe the state as of now -
            // otherwise those checks would be comparing against a record this test itself superseded.
            recorded.clear();
            report.check(mixManager.getRecoveryData(mixInfo.mixId, recorded).isOk(), "the record re-reads after the replacement export");
            report.check(static_cast<int>(recorded.size()) == kTrackCount,
                std::format("the record still has {} rows after re-export (found {})", kTrackCount, recorded.size()));

            // --- 3d. A record from before the mix length was stored ---

            {
                // Rows written under v27 have no total_duration, and the migration correctly leaves them
                // NULL. Staged here the same way the unknown JSON field was, because nothing reachable
                // through the public interfaces can produce a NULL any more.
                //
                // The failure this guards against is a confident nought: reading NULL as zero and then
                // printing #EXTMIXDURATION:0 would tell a reader that a two-hour mix is empty.
                const bool nulled = setRecoveryDurationToNull(databasePath, mixInfo.mixId);
                report.check(nulled, "a v27-style row with no recorded mix length could be staged");

                if (nulled)
                {
                    std::vector<MixRecoveryEntry> legacy;
                    report.check(mixManager.getRecoveryData(mixInfo.mixId, legacy).isOk(), "a record with no mix length still reads");
                    report.check(!legacy.empty() && !legacy.front().mixTotalDuration.has_value(),
                        "an unrecorded mix length reads back as unknown, not as zero");

                    if (!legacy.empty())
                    {
                        const auto legacyPath = selfTestRoot / "legacy-duration.m3u";
                        report.check(audio::writeMixRecoveryM3U(legacyPath, legacy, legacy.front().mixTotalDuration).empty(),
                            "a playlist can still be written from a record with no mix length");

                        std::string legacyText;
                        {
                            std::ifstream in{legacyPath, std::ios::binary};
                            legacyText.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
                        }
                        report.check(legacyText.find("#EXTMIXDURATION") == std::string::npos,
                            "the playlist omits the duration line rather than claiming zero");
                        report.check(legacyText.find("#EXTINF:") != std::string::npos, "the playlist is otherwise complete");
                    }
                }
            }

            // Put the mix back the way it was, so the checks below compare against a full record.
            {
                const auto reExport = exporter.exportMixToFile(mixInfo.mixId, settings, nullptr);
                report.check(reExport.success && reExport.recoveryWarning.empty(), "re-exporting restores a record with a known mix length");
                recorded.clear();
                std::ignore = mixManager.getRecoveryData(mixInfo.mixId, recorded);
            }

            // --- 4. A capture that does not match what was rendered is refused ---

            {
                auto doctored = liveMixTracks;
                if (!doctored.empty())
                {
                    doctored.front().cueStart += Duration_t{500};
                }

                MixRecoveryCapture capture;
                const auto captureResult = mixManager.captureRecoveryData(mixInfo.mixId, capture, &doctored);
                report.check(captureResult.isOk(), "a mismatched capture completes without error");
                report.check(!capture.captured, "a capture is refused when the mix no longer matches what was rendered");
                report.note(std::format("refusal said: {}", capture.skipReason));

                std::vector<MixRecoveryEntry> afterRefusal;
                std::ignore = mixManager.getRecoveryData(mixInfo.mixId, afterRefusal);
                // Every field, not just the count: a rewrite producing the same number of rows would slip
                // past a size check. capturedAt alone would catch it, since it is stamped afresh on every
                // capture, but comparing the lot costs nothing and says what is meant.
                report.check(sameRecord(recorded, afterRefusal), "a refused capture leaves the previous record identical, field for field");
            }

            // --- 5. Losing a track: the record survives, and cannot be overwritten by the damage ---

            // Deleting the track cascades its MixTracks row away, which is the exact damage that started
            // all of this. It leaves a gap in order_in_mix, because nothing renumbers on this path.
            const auto victimId = orderedTrackIds.front();
            if (!db.removeTracks({victimId}).isOk())
            {
                report.abort(std::format("Could not delete track {} to simulate the damage.", victimId));
                writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
                return 1;
            }
            report.note(std::format("deleted track {} from the library", victimId));

            report.check(mixManager.getMixTracks(mixInfo.mixId).size() == static_cast<size_t>(kTrackCount - 1),
                "deleting a track cascades its row out of the mix");

            std::vector<MixRecoveryEntry> afterTrackDelete;
            report.check(mixManager.getRecoveryData(mixInfo.mixId, afterTrackDelete).isOk(), "recovery data still reads after a track was deleted");
            report.check(sameRecord(recorded, afterTrackDelete),
                std::format("the recovery record survives the deletion of its tracks unchanged (expected {} rows, found {})",
                    recorded.size(),
                    afterTrackDelete.size()));

            {
                MixRecoveryCapture capture;
                const auto captureResult = mixManager.captureRecoveryData(mixInfo.mixId, capture);
                report.check(captureResult.isOk(), "capturing a damaged mix completes without error");
                report.check(!capture.captured, "a damaged mix is refused rather than captured");
                report.note(std::format("refusal said: {}", capture.skipReason));

                std::vector<MixRecoveryEntry> afterRefusal;
                std::ignore = mixManager.getRecoveryData(mixInfo.mixId, afterRefusal);
                report.check(sameRecord(recorded, afterRefusal),
                    "the complete record survives the damaged mix unchanged, field for field - this is the whole point");
            }

            // --- 5b. A damaged mix may be recorded as partial, but never over a good record ---
            //
            // The mixes this exists for lost rows before recovery data existed, and their missing
            // tracks are in no surviving backup. Refusing them forever left the mixes most at risk
            // as the only ones with nothing written down at all.
            //
            // The order of these two checks is the point. Allowing a partial capture must not weaken
            // the rule it sits next to: a mix that has already lost tracks must not be able to
            // overwrite a complete description of itself with a shorter one.
            {
                MixRecoveryCapture overwrite;
                const auto blocked = mixManager.captureRecoveryData(mixInfo.mixId, overwrite, nullptr, RecoveryCaptureMode::AllowIncomplete);
                report.check(blocked.isOk(), "a partial capture over an existing record completes without error");
                report.check(!overwrite.captured, "a partial capture is refused when a complete record already exists");
                report.note(std::format("refusal said: {}", overwrite.skipReason));

                std::vector<MixRecoveryEntry> stillThere;
                std::ignore = mixManager.getRecoveryData(mixInfo.mixId, stillThere);
                report.check(sameRecord(recorded, stillThere), "the complete record is untouched by the refused partial capture");

                // With no record in the way, the same mix records what survives.
                report.check(clearRecoveryData(databasePath, mixInfo.mixId), "the record could be cleared to leave the mix unprotected");

                // A second deletion, from the middle of what is left. The first victim was the track
                // at position 0, so the survivors run 1, 2, 3 - a gap at the front only, which never
                // falls between two recorded tracks. Removing the one at position 2 leaves 1 and 3,
                // and a hole between two rows that are both in the record is what the playlist note
                // below has to describe.
                report.check(db.removeTracks({orderedTrackIds[2]}).isOk(), "a second track could be deleted, from the middle");

                MixRecoveryCapture partial;
                const auto partialResult = mixManager.captureRecoveryData(mixInfo.mixId, partial, nullptr, RecoveryCaptureMode::AllowIncomplete);
                report.check(partialResult.isOk() && partial.captured, "a damaged mix with no record is captured when partial records are allowed");
                report.check(partial.incomplete, "the capture reports itself as partial");
                report.check(partial.entries.size() == static_cast<size_t>(kTrackCount - 2),
                    std::format("it records the {} rows that survived (recorded {})", kTrackCount - 2, partial.entries.size()));

                std::vector<MixRecoveryEntry> partialRead;
                report.check(mixManager.getRecoveryData(mixInfo.mixId, partialRead).isOk(), "the partial record reads back");
                report.check(!partialRead.empty() && std::none_of(partialRead.begin(),
                                                        partialRead.end(),
                                                        [](const MixRecoveryEntry &entry) { return entry.isComplete; }),
                    "every row of it is marked incomplete, so nothing reading it can mistake it for the whole mix");

                // Both positions, because they are different things and only one of them can be
                // reconstructed later. The record position is where the row sits here; the source
                // position is where the mix said the track was, and the jumps in it are the only
                // surviving evidence of what went missing and whereabouts.
                bool recordPositionsRun = !partialRead.empty();
                for (size_t i = 0; recordPositionsRun && i < partialRead.size(); ++i)
                {
                    recordPositionsRun = partialRead[i].orderInMix == static_cast<int>(i);
                }
                report.check(recordPositionsRun, "the record positions run 0..N-1");

                // Positions 0 and 2 were deleted, so the survivors sat at 1 and 3 in the mix. Both
                // gaps have to still be visible in the record: one before the first survivor, one
                // between the two.
                std::vector<int> sourcePositions;
                for (const auto &entry : partialRead)
                {
                    sourcePositions.push_back(entry.sourceOrderInMix.value_or(-1));
                }
                report.check(sourcePositions == std::vector<int>{1, 3},
                    std::format("the mix's own positions are kept, gaps and all (found {})", idsToText(sourcePositions)));

                // And the artefact meant to be read by a person says so too.
                //
                // Guarded, because front() on an empty vector is undefined behaviour and a test that
                // crashes is worse than one that fails: the run dies here and every check after this
                // point goes unreported, including the export refusal below.
                if (!partialRead.empty())
                {
                    const auto partialPath = selfTestRoot / "partial-record.m3u";
                    report.check(audio::writeMixRecoveryM3U(partialPath, partialRead, partialRead.front().mixTotalDuration).empty(),
                        "a playlist can be written from a partial record");

                    std::ifstream in{partialPath, std::ios::binary};
                    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
                    report.check(text.find("#EXTMIXINCOMPLETE:1") != std::string::npos, "the playlist declares itself incomplete");
                    report.check(text.find("WARNING") != std::string::npos, "and says so in words, for whoever opens it in an editor");

                    // The point of keeping the source positions: not "a track is missing" but where.
                    // Both holes, and by exact range - a substring that only proves some annotation
                    // exists would pass with one of the two missing, and the leading one is precisely
                    // the one a neighbour comparison cannot see.
                    report.check(text.find("1 track(s) missing here, at position(s) 0..0") != std::string::npos,
                        "the playlist names the hole before the first surviving track");
                    report.check(text.find("1 track(s) missing here, at position(s) 2..2") != std::string::npos,
                        "and the hole between the two surviving tracks");
                }

                // An export must never carry a partial record, whatever mode is asked for: the audio
                // file is finished, and a record beside it claims to list what is in it.
                const auto rendered = mixManager.getMixTracks(mixInfo.mixId);
                MixRecoveryCapture exportAttempt;
                const auto exportResult =
                    mixManager.captureRecoveryData(mixInfo.mixId, exportAttempt, &rendered, RecoveryCaptureMode::AllowIncomplete);
                report.check(exportResult.isOk(), "an export-time partial capture completes without error");
                report.check(!exportAttempt.captured, "a partial record is refused against a rendered export even when partials are allowed");
            }

            // --- 5c. A damaged mix can also hold two rows at the same position ---
            //
            // MixRecovery keys on (mix_id, order_in_mix); MixTracks does not, so a mix that lost rows
            // may also have repeats. Copying those positions across fails on the primary key, which
            // is exactly what happened to two mixes on the first real run of this. The record
            // renumbers instead: order_in_mix is the position within the record, which for an intact
            // mix it always was.
            {
                report.check(clearRecoveryData(databasePath, mixInfo.mixId), "the record could be cleared again");

                const auto before = mixManager.getMixTracks(mixInfo.mixId);
                report.check(before.size() >= 2, "the damaged mix still has rows to duplicate a position with");
                if (before.size() >= 2)
                {
                    report.check(duplicateOrderInMix(databasePath, mixInfo.mixId, before.back().orderInMix, before.front().orderInMix),
                        "two rows could be put at the same position");

                    MixRecoveryCapture repeated;
                    const auto repeatedResult =
                        mixManager.captureRecoveryData(mixInfo.mixId, repeated, nullptr, RecoveryCaptureMode::AllowIncomplete);
                    report.check(repeatedResult.isOk(), std::format("capturing a mix with a repeated position succeeds (said: '{}')", repeatedResult.errorMessage));
                    report.check(repeated.captured, "and it is captured rather than refused");
                    report.check(repeated.entries.size() == before.size(),
                        std::format("every surviving row is recorded ({} of {})", repeated.entries.size(), before.size()));

                    std::vector<MixRecoveryEntry> repeatedRead;
                    report.check(mixManager.getRecoveryData(mixInfo.mixId, repeatedRead).isOk(), "the record reads back");
                    bool numbered = repeatedRead.size() == before.size();
                    for (size_t i = 0; numbered && i < repeatedRead.size(); ++i)
                    {
                        numbered = repeatedRead[i].orderInMix == static_cast<int>(i);
                    }
                    report.check(numbered, "the recorded positions run 0..N-1, whatever the mix had stored");

                    // And the positions the mix held - including the repeat - are still there.
                    std::vector<int> heldPositions;
                    for (const auto &entry : repeatedRead)
                    {
                        heldPositions.push_back(entry.sourceOrderInMix.value_or(-1));
                    }
                    std::vector<int> expectedHeld;
                    for (const auto &row : before)
                    {
                        expectedHeld.push_back(row.orderInMix);
                    }
                    expectedHeld.back() = before.front().orderInMix; // the row that was moved on top
                    std::sort(expectedHeld.begin(), expectedHeld.end());
                    std::sort(heldPositions.begin(), heldPositions.end());
                    report.check(heldPositions == expectedHeld,
                        std::format("the duplicated source positions survive the renumbering (found {})", idsToText(heldPositions)));
                }
            }

            // --- 6. Deleting the mix does take its record with it ---

            report.check(mixManager.removeMix(mixInfo.mixId), "the test mix can be deleted");

            std::vector<MixRecoveryEntry> afterMixDelete;
            report.check(mixManager.getRecoveryData(mixInfo.mixId, afterMixDelete).isOk(), "recovery data reads without error after the mix was deleted");
            report.check(afterMixDelete.empty(), "deleting the mix removes its recovery rows");

            // --- 7. What the writer is allowed to overwrite ---
            //
            // Export-time writing replaces: the audio has just been rendered and the playlist beside
            // it has to describe that render. The maintenance pass must not, because the file it would
            // replace may be one somebody has been writing notes on while rebuilding a mix.
            {
                const auto occupied = selfTestRoot / "occupied.m3u";

                const std::string staged{"notes I made while putting this mix back together\nsecond line\n"};
                {
                    std::ofstream existing{occupied, std::ios::binary | std::ios::trunc};
                    existing << staged;
                }

                std::vector<MixRecoveryEntry> rows;
                std::ignore = mixManager.getRecoveryData(mixInfo.mixId, rows);
                if (rows.empty())
                {
                    // The mix was deleted just above, so build the one row this needs by hand.
                    MixRecoveryEntry entry;
                    entry.mixName = "a mix";
                    entry.filename = "track.mp3";
                    entry.folderPath = "D:\\music";
                    entry.duration = Duration_t{1000};
                    rows.push_back(entry);
                }

                bool existed = false;
                const auto refused = audio::writeMixRecoveryM3U(occupied, rows, std::nullopt, audio::M3UWriteMode::NeverReplace, &existed);
                // Every check below carries this, rather than relying on the ones above it. report.check
                // records a result; it does not stop the run, so a later line saying "and ..." is not
                // conditional on the earlier ones having held. Without it, each of them is separately true
                // of a writer that did nothing at all.
                const bool refusedCorrectly{refused.empty() && existed};
                report.check(refused.empty(), std::format("NeverReplace over an existing file is not an error (said: '{}')", refused));
                report.check(existed, "and it reports that the name was taken");

                {
                    std::ifstream in{occupied, std::ios::binary};
                    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
                    // The whole file, byte for byte. A prefix check passes for a writer that appended
                    // to it, or replaced everything after the first line - which is not "untouched".
                    report.check(refusedCorrectly && text == staged, "the file that was there is untouched, byte for byte - this is the whole point");
                }

                // No leftovers: a temporary beside it is a second name for whatever was published,
                // and the next thing to open that name truncates the file it points at.
                report.check(refusedCorrectly && strayTempCount(selfTestRoot) == 0, "and no temporary was left beside it");

                // A free name is claimed normally.
                const auto freeName = selfTestRoot / "unoccupied.m3u";
                existed = true;
                const auto written = audio::writeMixRecoveryM3U(freeName, rows, std::nullopt, audio::M3UWriteMode::NeverReplace, &existed);
                const bool published{written.empty() && !existed && std::filesystem::exists(freeName, ec)};
                report.check(written.empty() && !existed, "NeverReplace writes when the name is free");
                report.check(std::filesystem::exists(freeName, ec), "and the file is there afterwards");

                // This is the branch that publishes by hard link, so the temporary is a second name for
                // the file that was just published and has to be gone.
                //
                // !existed belongs in the gate as much as the rest: the already-there fast path also
                // returns no error, leaves the file in place and creates no temporary, so without it this
                // is green for a writer that took the branch this check is not about.
                report.check(published && strayTempCount(selfTestRoot) == 0, "and the temporary that became its second name is unlinked");

                // A name that is taken by something other than a usable file is a different answer from
                // "there is already a playlist here": no playlist can be written for that mix at all, so
                // it must be a failure rather than a quiet nothing-to-do.
                const auto blocked = selfTestRoot / "blocked.m3u";
                std::filesystem::create_directories(blocked, ec);
                report.check(std::filesystem::is_directory(blocked, ec), "a directory could be put where a playlist would go");
                report.check(audio::mixRecoveryM3UTargetState(blocked) == audio::M3UTargetState::Blocked,
                    "a directory under a playlist name reads as blocked, not as a playlist");

                existed = true;
                const auto refusedBlocked = audio::writeMixRecoveryM3U(blocked, rows, std::nullopt, audio::M3UWriteMode::NeverReplace, &existed);
                report.check(!refusedBlocked.empty(), "writing to a blocked name is an error, not a silent success");
                report.check(!existed, "and it is not reported as an ordinary already-there");

                report.check(audio::mixRecoveryM3UTargetState(freeName) == audio::M3UTargetState::HoldsFile,
                    "the playlist just written reads as a usable file");
                report.check(audio::mixRecoveryM3UTargetState(selfTestRoot / "never-written.m3u") == audio::M3UTargetState::Free,
                    "and a name nothing occupies reads as free");

                // Replace mode still replaces, which is what an export needs.
                const auto replaced = audio::writeMixRecoveryM3U(occupied, rows, std::nullopt, audio::M3UWriteMode::ReplaceExisting);
                report.check(replaced.empty(), "ReplaceExisting still replaces");
                {
                    std::ifstream in{occupied, std::ios::binary};
                    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
                    report.check(text.starts_with("#EXTM3U"), "and the replacement really is the playlist");
                }
            }

            writeResultsFile(resultsPath, "jucyaudio mix recovery self test", report);
            spdlog::info("[SelfTest] Mix recovery finished with {} failure(s). Results: {}", report.failures(), pathToString(resultsPath));
            return report.failures() == 0 ? 0 : 1;
        }

        int runMigrationSelfTest(const std::filesystem::path &selfTestRoot)
        {
            Report report;
            const auto workRoot = selfTestRoot / "migration";
            const auto resultsPath = selfTestRoot / "migration-results.txt";
            const auto dbPath = workRoot / "v29.db";

            spdlog::info("[SelfTest] Starting migration self test. Root: {}", pathToString(selfTestRoot));

            std::error_code ec;
            std::filesystem::remove_all(workRoot, ec);
            std::filesystem::create_directories(workRoot, ec);
            if (ec)
            {
                report.abort(std::format("Could not create {}: {}", pathToString(workRoot), ec.message()));
                writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                return 1;
            }

            // A database shaped the way v29 left one.
            //
            // Built by letting the application create a complete current schema and then putting one
            // table back the way it was, rather than hand-writing the two tables the migration reads.
            // A database containing only those two is not a database this code can open: connect()
            // initialises the folder cache afterwards, which queries Folders and fails - quietly, since
            // connect() does not look at the result. The test would then have reported a successful
            // migration of a database that could not really be opened at all.
            //
            // This is the one migration in the project that rewrites primary keys, and it does so to
            // records that cannot be regenerated - the mixes they describe have already lost the rows
            // in question. It gets a test of its own for that reason.
            {
                SqliteTrackDatabase fresh;
                const auto created = fresh.connect(dbPath);
                report.check(created.isOk(), std::format("a complete scratch schema could be created (said: '{}')", created.errorMessage));
                if (!created.isOk())
                {
                    writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                    return 1;
                }
            }

            {
                SqliteDatabase seed;
                if (!seed.open(pathToString(dbPath)))
                {
                    report.abort("Could not reopen the scratch database to age it.");
                    writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                    return 1;
                }

                // Only MixRecovery goes back: it is the only table the v30 rung touches. Dropping it
                // takes its indexes with it, which the migration neither reads nor recreates.
                //
                // The foreign key on mix_id is kept, so the fixture is the shape v29 really had, and
                // the two mixes it points at are created first. Enforcement is switched on
                // explicitly: SqliteDatabase::open does not set PRAGMA foreign_keys, so without this
                // the key would be recorded and ignored, and a seeding mistake that invented mixes
                // would go through unnoticed.
                const bool built =
                    seed.execute("PRAGMA foreign_keys = ON;") &&
                    seed.execute("INSERT INTO Mixes (mix_id, name) VALUES (1, 'Seed One'), (2, 'Seed Two');") &&
                    seed.execute("DROP TABLE MixRecovery;") &&
                    seed.execute("CREATE TABLE MixRecovery(mix_id INTEGER NOT NULL, order_in_mix INTEGER NOT NULL, "
                                 "captured_at INTEGER NOT NULL, mix_name TEXT NOT NULL, total_duration INTEGER, track_id INTEGER, "
                                 "artist_name TEXT, album_title TEXT, title TEXT, filename TEXT, folder_path TEXT, duration INTEGER, "
                                 "filesize_bytes INTEGER, bpm INTEGER, mix_data TEXT, is_complete INTEGER NOT NULL DEFAULT 1, "
                                 "PRIMARY KEY (mix_id, order_in_mix), "
                                 "FOREIGN KEY (mix_id) REFERENCES Mixes(mix_id) ON DELETE CASCADE);") &&
                    // The indexes go back too. Dropping the table took them with it, and a fixture
                    // without them cannot show that the migration leaves them standing - which is
                    // worth showing, because renumbering a primary key is exactly the kind of work
                    // that gets done by rebuilding a table and losing whatever hung off it.
                    seed.execute("CREATE INDEX idx_mixrecovery_track ON MixRecovery(track_id);") &&
                    seed.execute("CREATE INDEX idx_mixrecovery_fileident ON MixRecovery(filename, filesize_bytes);") &&
                    seed.execute("UPDATE SchemaInfo SET value = '29' WHERE key = 'schema_version';");
                report.check(built, "the scratch database could be put back into its v29 shape");
                if (!built)
                {
                    writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                    return 1;
                }

                // Mix 1 is intact: positions 0, 1, 2, and must come through completely unchanged.
                // Mix 2 is one of the damaged ones: positions 0, 3, 7 with is_complete = 0, exactly as
                // v29 wrote them. Its record positions have to become 0, 1, 2 while 0, 3, 7 survive as
                // the source positions - that is the whole point of the migration.
                const char *insert = "INSERT INTO MixRecovery (mix_id, order_in_mix, captured_at, mix_name, total_duration, track_id, "
                                     "artist_name, album_title, title, filename, folder_path, duration, filesize_bytes, bpm, mix_data, "
                                     "is_complete) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
                // Every value is distinct per row and derived from the mix and position it belongs to.
                // Identical payloads would let a field move from one row to another unnoticed, which is
                // exactly the mistake a renumbering migration is capable of making.
                const auto seedRow = [&seed, insert](int64_t mixId, int order, int complete)
                {
                    const auto tag = [mixId, order](std::string_view field)
                    {
                        return std::format("{}-{}-{}", field, mixId, order);
                    };
                    const auto number = [mixId, order](int64_t base)
                    {
                        return base + mixId * 1000 + order;
                    };

                    SqliteStatement stmt{seed, insert};
                    return stmt.isValid() && stmt.addParam(mixId) && stmt.addParam(order) && stmt.addParam(number(700000)) &&
                           stmt.addParam(tag("mix")) && stmt.addParam(number(400000)) && stmt.addParam(number(100)) &&
                           stmt.addParam(tag("artist")) && stmt.addParam(tag("album")) && stmt.addParam(tag("title")) &&
                           stmt.addParam(tag("file")) && stmt.addParam(tag("folder")) && stmt.addParam(number(1000)) &&
                           stmt.addParam(number(2000)) && stmt.addParam(number(120)) && stmt.addParam(tag("data")) &&
                           stmt.addParam(int64_t{complete}) && stmt.execute();
                };

                bool seeded = true;
                for (const auto order : {0, 1, 2})
                {
                    seeded = seeded && seedRow(1, order, 1);
                }
                for (const auto order : {0, 3, 7})
                {
                    seeded = seeded && seedRow(2, order, 0);
                }
                report.check(seeded, "an intact record and a gapped partial one could be seeded");
            }

            // Opening it runs the ladder. Nothing else in this test asks the database for anything, so
            // whatever comes back afterwards is the migration's doing.
            {
                SqliteTrackDatabase migrated;
                const auto connected = migrated.connect(dbPath);
                report.check(connected.isOk(), std::format("the v29 database migrates on open (said: '{}')", connected.errorMessage));
            }

            SqliteDatabase check;
            if (!check.open(pathToString(dbPath)))
            {
                report.abort("Could not reopen the migrated database.");
                writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                return 1;
            }

            {
                SqliteStatement stmt{check, "SELECT value FROM SchemaInfo WHERE key = 'schema_version';"};
                // The latest version, not 30: opening a v29 database runs every rung above it, and this
                // fixture is only shaped for the v30 one. The checks below are what say v30 did its job.
                report.check(stmt.getNextResult() && stmt.getText(0) == "31", "the schema is stamped at the latest version");
            }

            // Read back whole rows, compared as text with NULL spelled out.
            //
            // Not a SELECT COUNT(*) ... WHERE field <> 'expected': in SQL, NULL <> anything is unknown
            // rather than true, so the WHERE discards it and a migration that had blanked every title
            // in the table would have been reported as leaving them all alone.
            const auto rowsOf = [&check](MixId mixId)
            {
                std::vector<std::string> rows;
                SqliteStatement stmt{check,
                    "SELECT order_in_mix, source_order_in_mix, captured_at, mix_name, total_duration, track_id, artist_name, "
                    "album_title, title, filename, folder_path, duration, filesize_bytes, bpm, mix_data, is_complete "
                    "FROM MixRecovery WHERE mix_id = ? ORDER BY order_in_mix"};
                if (!stmt.isValid() || !stmt.addParam(mixId))
                {
                    return rows;
                }

                while (stmt.getNextResult())
                {
                    std::string row;
                    for (int col = 0; col < 16; ++col)
                    {
                        // "<null>" rather than an empty string, so a field that was blanked cannot
                        // read as a field that was always empty.
                        row += (col == 0 ? "" : "|") + (stmt.isNull(col) ? std::string{"<null>"} : stmt.getText(col));
                    }
                    rows.push_back(std::move(row));
                }
                return rows;
            };

            const auto expectedRow = [](int64_t mixId, int recordOrder, int sourceOrder, int complete)
            {
                const auto tag = [mixId, sourceOrder](std::string_view field)
                {
                    return std::format("{}-{}-{}", field, mixId, sourceOrder);
                };
                const auto number = [mixId, sourceOrder](int64_t base)
                {
                    return base + mixId * 1000 + sourceOrder;
                };

                return std::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}",
                    recordOrder,
                    sourceOrder,
                    number(700000),
                    tag("mix"),
                    number(400000),
                    number(100),
                    tag("artist"),
                    tag("album"),
                    tag("title"),
                    tag("file"),
                    tag("folder"),
                    number(1000),
                    number(2000),
                    number(120),
                    tag("data"),
                    complete);
            };

            // The intact record: positions unchanged, because rank and stored position already agreed,
            // and every field still attached to the row it was seeded on.
            const std::vector<std::string> expectedIntact{expectedRow(1, 0, 0, 1), expectedRow(1, 1, 1, 1), expectedRow(1, 2, 2, 1)};
            report.check(rowsOf(1) == expectedIntact, "an intact record comes through the migration unchanged, field for field");

            // The damaged one: renumbered to 0, 1, 2 while 0, 3, 7 survive beside it - and each row's
            // payload still names the position it originally held, so nothing was shuffled.
            const std::vector<std::string> expectedGapped{expectedRow(2, 0, 0, 0), expectedRow(2, 1, 3, 0), expectedRow(2, 2, 7, 0)};
            report.check(rowsOf(2) == expectedGapped, "a gapped record is renumbered 0..N-1 with its source positions and payload intact");

            if (rowsOf(2) != expectedGapped)
            {
                for (const auto &row : rowsOf(2))
                {
                    report.note("gapped row: " + row);
                }
            }

            {
                SqliteStatement stmt{check, "SELECT COUNT(*) FROM MixRecovery"};
                report.check(stmt.getNextResult() && stmt.getInt64(0) == 6, "no row was lost or duplicated");
            }

            {
                SqliteStatement stmt{check,
                    "SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND tbl_name = 'MixRecovery' "
                    "AND name IN ('idx_mixrecovery_track', 'idx_mixrecovery_fileident')"};
                report.check(stmt.getNextResult() && stmt.getInt64(0) == 2, "both MixRecovery indexes are still there afterwards");
            }

            // --- v30 to v31: one Folders row per path ---
            //
            // Its own database, because the fixture is a library that already carries the damage the
            // index exists to prevent: two Folders rows for one path, tracks under both, one filename
            // present in both, a child folder hanging off the row that is about to go, and a mix
            // pointing at a track in it.
            //
            // The merge is what needs the test, not the index. Folding two folders onto one moves
            // tracks into a folder that may already hold a row for the same filename, and the obvious
            // way to get there - delete the duplicate and let ON DELETE CASCADE tidy up - takes those
            // tracks and the MixTracks rows referencing them with it, silently.
            {
                const auto folderDbPath = workRoot / "v30-folders.db";

                {
                    SqliteTrackDatabase fresh;
                    const auto created = fresh.connect(folderDbPath);
                    report.check(created.isOk(), std::format("a scratch schema for the folder migration could be created (said: '{}')", created.errorMessage));
                    if (!created.isOk())
                    {
                        writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                        return 1;
                    }
                }

                {
                    SqliteDatabase seed;
                    if (!seed.open(pathToString(folderDbPath)))
                    {
                        report.abort("Could not reopen the folder scratch database to age it.");
                        writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                        return 1;
                    }

                    // Dropping the index is what puts the database back into its v30 shape - it is the
                    // only thing v31 adds, and the duplicate rows below cannot be inserted while it
                    // stands. Foreign keys stay off here on purpose: the fixture is written by hand and
                    // is consistent, and the migration is what has to keep it that way.
                    const bool aged =
                        seed.execute("DROP INDEX idx_folders_root_path;") &&
                        // Rows 20 and 21 are the other kind of duplicate: neither has a computed path,
                        // so a unique index on root_path cannot see them (NULLs are distinct) and the
                        // migration has to reconstruct their paths before it looks for duplicates.
                        // Their names differ only in case, which is a duplicate to normalizeForCache
                        // and therefore to the cache - the reconstruction has to use the same rule.
                        // Three rows for c:\dup, not two: with only two, a title collision always has
                        // one album on the keeper, and the case where neither is - two losers holding
                        // the same title and the keeper holding none - never arises. That is the case
                        // where "whichever the UPDATE reached first" would be the answer if the merge
                        // did not pick one itself.
                        seed.execute("INSERT INTO Folders (folder_id, parent_id, name, root_path, actual_path) VALUES "
                                     "(10, NULL, 'dup', 'c:\\dup', 'C:\\Dup'), "
                                     "(11, NULL, 'dup', 'c:\\dup', 'C:\\Dup'), "
                                     "(12, 11, 'sub', 'c:\\dup\\sub', 'C:\\Dup\\Sub'), "
                                     "(13, NULL, 'dup', 'c:\\dup', 'C:\\Dup'), "
                                     "(20, NULL, 'Orphan', NULL, NULL), "
                                     "(21, NULL, 'ORPHAN', '', NULL), "
                                     "(22, 20, 'Deep', NULL, NULL), "
                                     "(23, 21, 'DEEP', NULL, NULL);") &&
                        seed.execute("INSERT INTO Tracks (track_id, folder_id, filename, title) VALUES "
                                     "(100, 10, 'both.mp3', 'kept'), "
                                     "(101, 11, 'both.mp3', 'collapsed'), "
                                     "(102, 11, 'only-here.mp3', 'moved'), "
                                     "(103, 12, 'child.mp3', 'reparented'), "
                                     "(104, 20, 'a.mp3', 'under the surviving pathless row'), "
                                     "(105, 21, 'b.mp3', 'under the other pathless row'), "
                                     "(106, 13, 'c.mp3', 'under the third row for one path'), "
                                     "(107, 11, 'd.mp3', 'on the album that wins a two-loser collision'), "
                                     "(108, 22, 'e.mp3', 'under the surviving pathless child'), "
                                     "(109, 23, 'f.mp3', 'under the other pathless child'), "
                                     "(110, 11, 'g.mp3', 'on the album that has no counterpart');") &&
                        // Albums, covering the three shapes the merge has to tell apart. genres, moods
                        // and tags are JSON arrays - that is what vectorToJsonArray writes and what
                        // jsonArrayToVector expects - so the fixture stores them that way.
                        //
                        // 200 and 201 collide by title with one of them on the keeper: 201 gives way to
                        // 200 (lower id) rather than cascading away with folder 11. 200 carries no
                        // genres, tags, bandcamp link or year; 201 carries all four, and an
                        // album_artist that 200 also has, so the survivor keeps its own there.
                        //
                        // 204 and 205 collide by title with neither on the keeper - one on each of the
                        // two loser rows. 204 wins by id, and has to be moved to folder 10 afterwards.
                        //
                        // 202 has no counterpart at all and simply moves, landing beside 200 in folder
                        // 10. A folder holding two albums of different titles is a layout the schema
                        // allows and the fixture is deliberately built to produce.
                        seed.execute("INSERT INTO Albums (album_id, album_artist, title, year, folder_id, genres, moods, tags, bandcamp_url) VALUES "
                                     "(200, 'Keeper Artist', 'Shared', NULL, 10, '[]', NULL, NULL, NULL), "
                                     "(201, 'Loser Artist', 'Shared', 1999, 11, '[\"downtempo\"]', '[\"calm\"]', '[\"mine\"]', 'https://example.test/album'), "
                                     "(202, 'Solo Artist', 'Only There', 2001, 11, '[\"ambient\"]', NULL, NULL, NULL), "
                                     "(204, 'Loser One', 'Two Losers', NULL, 11, '[]', NULL, '[\"from-204\"]', NULL), "
                                     "(205, 'Loser Two', 'Two Losers', 1990, 13, '[\"acid\"]', NULL, '[\"from-205\"]', NULL);") &&
                        seed.execute("UPDATE Tracks SET album_id = 200 WHERE track_id = 100;") &&
                        seed.execute("UPDATE Tracks SET album_id = 201 WHERE track_id IN (101, 102);") &&
                        seed.execute("UPDATE Tracks SET album_id = 202 WHERE track_id = 110;") &&
                        seed.execute("UPDATE Tracks SET album_id = 204 WHERE track_id = 107;") &&
                        seed.execute("UPDATE Tracks SET album_id = 205 WHERE track_id = 106;") &&
                        seed.execute("INSERT INTO Mixes (mix_id, name) VALUES (7, 'Folder Seed Mix');") &&
                        seed.execute("INSERT INTO MixTracks (mix_id, track_id, order_in_mix, mix_data) VALUES "
                                     "(7, 101, 0, '{}'), (7, 102, 1, '{}');") &&
                        // TrackMarkers by hand, because initialSqlStatements does not create it - only
                        // the v4 rung does, and a database created from scratch never runs the ladder.
                        // That divergence is its own entry in tasks.md; here it means the fixture has to
                        // put the table back to cover the step that remaps markers. The FTS index is
                        // deliberately left absent, so this one fixture exercises both sides of the
                        // "skip a step whose table is not there" check in the v31 rung.
                        seed.execute("CREATE TABLE TrackMarkers (marker_id INTEGER PRIMARY KEY AUTOINCREMENT, track_id INTEGER NOT NULL, "
                                     "position_ms INTEGER NOT NULL, comment TEXT NOT NULL, created_at INTEGER NOT NULL, "
                                     "updated_at INTEGER NOT NULL, color TEXT, emoji TEXT, "
                                     "FOREIGN KEY (track_id) REFERENCES Tracks(track_id) ON DELETE CASCADE);") &&
                        seed.execute("INSERT INTO TrackMarkers (marker_id, track_id, position_ms, comment, created_at, updated_at) VALUES "
                                     "(1, 101, 5000, 'on the collapsed row', 1, 1);") &&
                        seed.execute("UPDATE SchemaInfo SET value = '30' WHERE key = 'schema_version';");
                    report.check(aged, "a v30-shaped database holding duplicate folder rows could be seeded");
                    if (!aged)
                    {
                        writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                        return 1;
                    }
                }

                {
                    SqliteTrackDatabase migrated;
                    const auto connected = migrated.connect(folderDbPath);
                    report.check(connected.isOk(), std::format("the v30 database migrates on open (said: '{}')", connected.errorMessage));
                }

                SqliteDatabase check;
                if (!check.open(pathToString(folderDbPath)))
                {
                    report.abort("Could not reopen the migrated folder database.");
                    writeResultsFile(resultsPath, "jucyaudio migration self test", report);
                    return 1;
                }

                // One value per query, as text with NULL spelled out, so a column that was blanked
                // cannot read as one that was always empty.
                const auto scalar = [&check](const char *sql)
                {
                    SqliteStatement stmt{check, sql};
                    if (!stmt.isValid() || !stmt.getNextResult())
                    {
                        return std::string{"<query failed>"};
                    }
                    return stmt.isNull(0) ? std::string{"<null>"} : stmt.getText(0);
                };

                report.check(scalar("SELECT value FROM SchemaInfo WHERE key = 'schema_version'") == "31", "the schema is stamped at version 31");
                report.check(scalar("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = 'idx_folders_root_path'") == "1",
                    "the unique folder path index is there afterwards");
                report.check(scalar("SELECT COUNT(*) FROM Folders WHERE root_path = 'c:\\dup'") == "1", "one row is left for the duplicated path");
                report.check(scalar("SELECT folder_id FROM Folders WHERE root_path = 'c:\\dup'") == "10", "and it is the lowest of the two ids");
                report.check(scalar("SELECT parent_id FROM Folders WHERE folder_id = 12") == "10", "the child of the row that went is re-parented onto the survivor");

                // The tracks that must still be there, and where. 101 is the collapsed one: same folder
                // path and same filename as 100, so it is the same file described twice.
                report.check(scalar("SELECT COUNT(*) FROM Tracks") == "10", "the merge lost no track it could not prove was a duplicate");
                report.check(scalar("SELECT folder_id FROM Tracks WHERE track_id = 100") == "10", "the surviving copy of the shared filename stayed put");
                report.check(scalar("SELECT COUNT(*) FROM Tracks WHERE track_id = 101") == "0", "its duplicate was collapsed away");
                report.check(scalar("SELECT folder_id FROM Tracks WHERE track_id = 102") == "10", "a track with no counterpart moved to the survivor");
                report.check(scalar("SELECT folder_id FROM Tracks WHERE track_id = 103") == "12", "a track in the re-parented child did not move");

                // The point of merging rather than cascading. MixTracks.track_id cascades on delete, so
                // a migration that removed 101 without remapping would have silently shortened this mix.
                report.check(scalar("SELECT COUNT(*) FROM MixTracks WHERE mix_id = 7") == "2", "the mix still has both of its rows");
                report.check(scalar("SELECT track_id FROM MixTracks WHERE mix_id = 7 AND order_in_mix = 0") == "100",
                    "the row that pointed at the collapsed track now points at the one it kept");
                report.check(scalar("SELECT track_id FROM MixTracks WHERE mix_id = 7 AND order_in_mix = 1") == "102", "the other row is untouched");
                report.check(scalar("SELECT track_id FROM TrackMarkers WHERE marker_id = 1") == "100", "a marker on the collapsed track followed it to the one it kept");

                // Two rows that never had a path. Nothing in the schema can see them as duplicates, so
                // the migration has to reconstruct what they name before it looks - and reconstruct it
                // with normalizeForCache, or 'Orphan' and 'ORPHAN' stay two folders forever.
                report.check(scalar("SELECT COUNT(*) FROM Folders WHERE root_path = 'orphan'") == "1",
                    "two rows that never had a computed path are reconstructed and merged into one");
                report.check(scalar("SELECT folder_id FROM Folders WHERE root_path = 'orphan'") == "20", "and the survivor is the lowest of the two ids");
                report.check(scalar("SELECT COUNT(*) FROM Folders WHERE root_path IS NULL OR root_path = ''") == "0",
                    "no row is left with a path the index cannot see");
                report.check(scalar("SELECT folder_id FROM Tracks WHERE track_id = 104") == "20", "the track under the survivor stayed there");
                report.check(scalar("SELECT folder_id FROM Tracks WHERE track_id = 105") == "20", "the track under the other one came across");

                // The same thing one level down, which is the branch that reconstructs a path from a
                // parent rather than from a name alone: two pathless children under the two pathless
                // roots, whose names also differ only in case.
                report.check(scalar("SELECT COUNT(*) FROM Folders WHERE root_path = 'orphan\\deep'") == "1",
                    "two pathless children under two pathless parents reconstruct to one path and merge");
                report.check(scalar("SELECT folder_id FROM Folders WHERE root_path = 'orphan\\deep'") == "22", "the survivor is the lowest of the two ids");
                report.check(scalar("SELECT parent_id FROM Folders WHERE folder_id = 22") == "20", "and hangs off the folder its parent was merged into");
                report.check(scalar("SELECT folder_id FROM Tracks WHERE track_id = 108") == "22", "the track under the surviving child stayed there");
                report.check(scalar("SELECT folder_id FROM Tracks WHERE track_id = 109") == "22", "the track under the other child came across");
                report.check(scalar("SELECT COUNT(*) FROM Folders") == "4", "four folders are left, one per distinct path");

                // The albums. 201 could not move - 200 holds that title on the keeper folder - and had
                // to be merged rather than left to cascade away with folder 11.
                report.check(scalar("SELECT COUNT(*) FROM Albums") == "3", "the two colliding albums were merged away and three survive");
                report.check(scalar("SELECT folder_id FROM Albums WHERE album_id = 200") == "10", "the survivor of the collision with the keeper is the keeper's");
                report.check(scalar("SELECT COUNT(*) FROM Albums WHERE album_id = 201") == "0", "the album that could not move is gone");
                report.check(scalar("SELECT genres FROM Albums WHERE album_id = 200") == "[\"downtempo\"]", "the survivor took the genres it had none of");
                report.check(scalar("SELECT moods FROM Albums WHERE album_id = 200") == "[\"calm\"]", "and the moods");
                report.check(scalar("SELECT tags FROM Albums WHERE album_id = 200") == "[\"mine\"]", "and the tags");
                report.check(scalar("SELECT bandcamp_url FROM Albums WHERE album_id = 200") == "https://example.test/album", "and the bandcamp link");
                report.check(scalar("SELECT year FROM Albums WHERE album_id = 200") == "1999", "and the year");
                report.check(scalar("SELECT album_artist FROM Albums WHERE album_id = 200") == "Keeper Artist",
                    "but kept its own value where it had one");
                report.check(scalar("SELECT album_id FROM Tracks WHERE track_id = 100") == "200", "the track already on the survivor still points at it");
                report.check(scalar("SELECT album_id FROM Tracks WHERE track_id = 102") == "200",
                    "and the track on the merged-away album was moved onto it, not orphaned");

                // Two losers, no album of that title on the keeper. Which one survives is decided by
                // id, not by the order the statements happened to reach them.
                report.check(scalar("SELECT COUNT(*) FROM Albums WHERE title = 'Two Losers'") == "1", "a collision between two losers leaves one album");
                report.check(scalar("SELECT album_id FROM Albums WHERE title = 'Two Losers'") == "204", "and it is the lower of the two ids, whichever was reached first");
                report.check(scalar("SELECT folder_id FROM Albums WHERE album_id = 204") == "10", "the survivor was moved onto the keeper folder");
                report.check(scalar("SELECT genres FROM Albums WHERE album_id = 204") == "[\"acid\"]", "it took the genres it had none of from the other loser");
                report.check(scalar("SELECT year FROM Albums WHERE album_id = 204") == "1990", "and the year");
                report.check(scalar("SELECT tags FROM Albums WHERE album_id = 204") == "[\"from-204\"]", "and kept its own tags");
                report.check(scalar("SELECT album_id FROM Tracks WHERE track_id = 107") == "204", "its own track still points at it");
                report.check(scalar("SELECT album_id FROM Tracks WHERE track_id = 106") == "204", "and the other loser's track was moved onto it");

                // The album with no counterpart moves with its folder, id, metadata and tracks intact -
                // landing beside 200 in folder 10, which is a folder holding two albums of different
                // titles. The schema allows that, so the migration must not treat it as a collision.
                report.check(scalar("SELECT folder_id FROM Albums WHERE album_id = 202") == "10", "an album with no counterpart moved to the survivor");
                report.check(scalar("SELECT genres FROM Albums WHERE album_id = 202") == "[\"ambient\"]", "keeping what was on it");
                report.check(scalar("SELECT album_id FROM Tracks WHERE track_id = 110") == "202", "and the track that pointed at it still does");
                report.check(scalar("SELECT COUNT(*) FROM Albums WHERE folder_id = 10") == "3",
                    "one folder holds three albums of different titles afterwards, which is a layout the schema allows");

                // And the index actually refuses, rather than merely existing.
                {
                    SqliteStatement stmt{check, "INSERT INTO Folders (parent_id, name, root_path) VALUES (NULL, 'dup', 'c:\\dup');"};
                    report.check(stmt.isValid() && !stmt.execute(), "a second row for a path the table already has is refused");
                }
            }

            writeResultsFile(resultsPath, "jucyaudio migration self test", report);
            spdlog::info("[SelfTest] Migration finished with {} failure(s). Results: {}", report.failures(), pathToString(resultsPath));
            return report.failures() == 0 ? 0 : 1;
        }

        int runBackupSelfTest(const std::filesystem::path &selfTestRoot)
        {
            Report report;
            const auto workRoot = selfTestRoot / "backup";
            const auto resultsPath = selfTestRoot / "backup-results.txt";
            const auto dbPath = workRoot / "walcheck.db";

            spdlog::info("[SelfTest] Starting backup self test. Root: {}", pathToString(selfTestRoot));

            std::error_code ec;
            std::filesystem::remove_all(workRoot, ec);
            std::filesystem::create_directories(workRoot, ec);
            if (ec)
            {
                report.abort(std::format("Could not create {}: {}", pathToString(workRoot), ec.message()));
                writeResultsFile(resultsPath, "jucyaudio backup self test", report);
                return 1;
            }

            // A row that exists only in the -wal file. The connection stays open across the backup, so
            // nothing checkpoints it into the main database - which is precisely the state a copy of that
            // main file would fail to capture, and the state a real library is in most of the time.
            constexpr const char *kMarker = "only-in-the-wal";
            {
                SqliteDatabase db;
                if (!db.open(pathToString(dbPath)))
                {
                    report.abort(std::format("Could not create the scratch database {}", pathToString(dbPath)));
                    writeResultsFile(resultsPath, "jucyaudio backup self test", report);
                    return 1;
                }

                const bool prepared = db.execute("PRAGMA journal_mode=WAL;") && db.execute("CREATE TABLE WalCheck (marker TEXT NOT NULL);") &&
                                      db.execute("INSERT INTO WalCheck (marker) VALUES ('only-in-the-wal');");
                report.check(prepared, "a WAL-mode scratch database was created with a committed row");
                if (!prepared)
                {
                    writeResultsFile(resultsPath, "jucyaudio backup self test", report);
                    return 1;
                }

                // Appended to the path rather than rebuilt from a string: pathToString hands back UTF-8,
                // and feeding that to the narrow path constructor puts it straight back through the
                // active code page - the very conversion this codebase uses pathToString to avoid.
                auto walPath = dbPath;
                walPath += "-wal";
                const auto walSize = std::filesystem::exists(walPath, ec) ? std::filesystem::file_size(walPath, ec) : 0;
                report.check(walSize > 0, std::format("the committed row is still in the -wal file ({} bytes), not the database", walSize));

                // Backed up while that connection is still open, exactly as it would be with the app
                // running. Forced, because there are no existing backups to age out and this test is
                // about the mechanism rather than the schedule.
                config::RootSettings settings;
                DatabaseBackupManager manager;
                const auto outcome = manager.performBackupCheck(settings, dbPath, false, true, true);

                report.check(outcome.attempted, "the backup manager attempted a backup when forced");
                report.check(outcome.succeeded, std::format("the backup reports success (error: '{}')", outcome.errorMessage));
                report.check(!outcome.backupFile.empty() && std::filesystem::exists(outcome.backupFile, ec), "the backup file exists");

                // Nothing half-finished left lying around under a name that would later be counted,
                // pruned against, and one day restored from.
                // Any .partial at all, not one predicted name: temporaries carry a unique per-attempt
                // suffix now, so checking a computed path would be checking one that never existed.
                size_t partials = 0;
                for (const auto &entry : std::filesystem::directory_iterator{workRoot, ec})
                {
                    partials += (entry.path().extension() == ".partial") ? 1 : 0;
                }
                report.check(partials == 0, std::format("no .partial file was left behind (found {})", partials));
                // Distinct from succeeded: a backup can be published correctly and still leave a
                // temporary behind if something held a handle on it. That is a warning, not a failure,
                // and the two should not be conflated here either.
                report.check(outcome.warningMessage.empty(), std::format("the backup reports no housekeeping warning (got: '{}')", outcome.warningMessage));

                if (outcome.succeeded)
                {
                    // The check the whole item exists for. A copy_file backup opens, has a WalCheck
                    // table, and has no rows in it.
                    SqliteDatabase restored;
                    if (restored.open(pathToString(outcome.backupFile)))
                    {
                        std::string found;
                        SqliteStatement stmt{restored};
                        const bool queried = stmt.query(
                            [&found, &stmt]() -> bool
                            {
                                found = stmt.getText(0);
                                return true;
                            },
                            "SELECT marker FROM WalCheck;");
                        report.check(queried && found == kMarker,
                            std::format("the backup contains the row that was only in the WAL (found '{}')", found));
                    }
                    else
                    {
                        report.check(false, "the backup file could be opened as a database");
                    }
                }
            }

            writeResultsFile(resultsPath, "jucyaudio backup self test", report);
            spdlog::info("[SelfTest] Backup test finished with {} failure(s). Results: {}", report.failures(), pathToString(resultsPath));
            return report.failures() == 0 ? 0 : 1;
        }

        int runTimelineSelfTest(const std::filesystem::path &selfTestRoot)
        {
            Report report;
            // Its own library again: this suite writes to the mix it builds, and the checks are about
            // what a second writer does to a timeline that is already showing it.
            const auto workRoot = selfTestRoot / "timeline-library";
            const auto resultsPath = selfTestRoot / "timeline-results.txt";

            spdlog::info("[SelfTest] Starting timeline self test. Root: {}", pathToString(selfTestRoot));

            const auto stop = [&report, &resultsPath](const std::string &why)
            {
                report.abort(why);
                writeResultsFile(resultsPath, "jucyaudio timeline self test", report);
                return 1;
            };

            std::error_code ec;
            std::filesystem::remove_all(workRoot, ec);
            if (ec)
            {
                return stop(std::format("Could not clear {}: {}", pathToString(workRoot), ec.message()));
            }

            const auto albumPath = workRoot / kAlbumFolder;
            std::filesystem::create_directories(albumPath, ec);
            if (ec)
            {
                return stop(std::format("Could not create {}: {}", pathToString(albumPath), ec.message()));
            }

            for (int i = 1; i <= kTrackCount; ++i)
            {
                const auto file = albumPath / std::format("tl{:02}.wav", i);
                if (!writeSilentWav(file, static_cast<uint32_t>(44100 * kFixtureDurationMs / 1000)))
                {
                    return stop(std::format("Could not write the fixture {}", pathToString(file)));
                }
            }

            auto &db = theTrackLibrary.getTrackDatabase();
            auto &mixManager = theTrackLibrary.getMixManager();

            if (!db.getLibraryRootManager().addRoot(pathToString(workRoot)).has_value())
            {
                return stop("Could not add the timeline scratch library as a library root.");
            }

            const auto rootFolderId = db.getFolderDatabase().findOrCreateFolderByPath(workRoot);
            if (rootFolderId <= 0 || !runScan({rootFolderId}, false, report, "timeline fixture discovery"))
            {
                return stop("Could not discover the timeline scratch library.");
            }

            const auto tracks = tracksUnder(db, rootFolderId);
            if (static_cast<int>(tracks.size()) != kTrackCount)
            {
                return stop(std::format("Expected {} fixture tracks, found {}.", kTrackCount, tracks.size()));
            }

            std::vector<MixTrack> mixTracks;
            for (const auto &entry : tracks)
            {
                MixTrack mixTrack{};
                mixTrack.trackId = entry.second.trackId;
                mixTrack.orderInMix = static_cast<int>(mixTracks.size());
                mixTrack.cueStart = Duration_t{10 * (mixTrack.orderInMix + 1)};
                mixTrack.cueEnd = Duration_t{kFixtureDurationMs};
                mixTracks.push_back(mixTrack);
            }

            MixInfo mixInfo{};
            mixInfo.name = "SelfTest Timeline Mix";
            mixInfo.totalDuration = Duration_t{kTrackCount * kFixtureDurationMs};
            if (!mixManager.createOrUpdateMix(mixInfo, mixTracks) || mixInfo.mixId <= 0)
            {
                return stop("Could not create the timeline fixture mix.");
            }
            const auto mixId = mixInfo.mixId;
            report.note(std::format("built a {}-track mix (id {}) over its own scratch library", kTrackCount, mixId));

            // --- 1. The loader says when its rows changed ---
            //
            // This is what the timeline's protection is built on, so it is checked on its own first: a
            // guard comparing a number that never moves is not a guard.
            audio::MixProjectLoader loader;
            if (!loader.loadMix(mixId))
            {
                return stop("Could not load the fixture mix into a MixProjectLoader.");
            }

            const auto afterFirstLoad = loader.getContentsGeneration();
            if (!loader.reloadFromDatabase())
            {
                return stop("Could not reload the fixture mix.");
            }
            const auto afterReload = loader.getContentsGeneration();
            report.check(afterReload != afterFirstLoad,
                std::format("a reload changes the contents generation ({} -> {})", afterFirstLoad, afterReload));

            // Through reorderTracks, the public way in: a single move is handed straight to
            // reorderSingleTrack, which is where the rows are moved and the generation is bumped.
            const auto firstTrackId = loader.getMixTracks().front().trackId;
            report.check(loader.reorderTracks({{firstTrackId, 0}}), "a reorder to the position a track already holds succeeds");
            report.check(loader.getContentsGeneration() == afterReload, "that no-op reorder leaves the generation alone");

            report.check(loader.reorderTracks({{firstTrackId, 2}}), "a reorder that actually moves a track succeeds");
            const auto afterReorder = loader.getContentsGeneration();
            report.check(afterReorder != afterReload,
                std::format("an in-place reorder changes the generation ({} -> {})", afterReload, afterReorder));

            // The third way the rows change, and the third bump: removeTrackAtOrder drops a row and
            // renumbers the rest in memory, without a load and without touching the database.
            const auto rowsBeforeRemoval = loader.getMixTracks().size();
            report.check(loader.removeTrackAtOrder(0), "removing a row from the loaded mix in place succeeds");
            report.check(loader.getMixTracks().size() + 1 == rowsBeforeRemoval,
                std::format("that removal took one row out ({} -> {})", rowsBeforeRemoval, loader.getMixTracks().size()));
            const auto afterRemoval = loader.getContentsGeneration();
            report.check(afterRemoval != afterReorder,
                std::format("an in-place removal changes the generation ({} -> {})", afterReorder, afterRemoval));

            // Back to what the database holds, so the checks below start from a loader that agrees
            // with it - the removal above was in memory only, and the reorder was never saved.
            if (!loader.reloadFromDatabase())
            {
                return stop("Could not reload the fixture mix after the reorder checks.");
            }

            // --- 2. The views survive a reload nobody told the timeline about ---
            //
            // The layout pass reads both halves of every TrackView. While those were pointers into the
            // loader's vectors, the reload below freed everything they addressed and this walk read it.
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            juce::AudioThumbnailCache thumbnails{16};
            ui::TimelineComponent timeline{formats, thumbnails};
            timeline.setSize(4000, 600);

            if (!timeline.populateFrom(&loader))
            {
                return stop("The timeline could not be populated from the fixture mix.");
            }
            report.check(timeline.getNumChildComponents() == kTrackCount,
                std::format("the timeline built {} track components (found {})", kTrackCount, timeline.getNumChildComponents()));

            const auto generationBeforeTheReload = loader.getContentsGeneration();
            if (!loader.reloadFromDatabase())
            {
                return stop("Could not reload the mix behind the back of the timeline.");
            }
            report.check(loader.getContentsGeneration() != generationBeforeTheReload,
                "that reload is visible in the generation, which is what the timeline compares against");

            // No repopulation in between, deliberately. Nothing here can assert the absence of a
            // use-after-free - a released allocation often still reads back fine - so what is asserted
            // is that the walk completes and the views are still there afterwards.
            // Through setSize, because resized() is private: a new width runs the same layout pass,
            // which is the walk over every view that used to read the freed vectors.
            timeline.setSize(4200, 600);
            report.check(timeline.getNumChildComponents() == kTrackCount, "the layout pass after that reload leaves the track components intact");

            // --- 3. An edit whose positions came from stale views is refused ---

            // Repopulated first, so the clipboard copy is taken from a timeline that agrees with the
            // loader: what is being tested below is the paste, not the copy.
            if (!timeline.populateFrom(&loader))
            {
                return stop("The timeline could not be repopulated before the clipboard check.");
            }

            auto *const firstComponent = dynamic_cast<ui::MixTrackComponent *>(timeline.getChildComponent(0));
            if (firstComponent == nullptr)
            {
                return stop("The first child of the timeline is not a track component.");
            }
            timeline.setSelectedTrack(firstComponent);
            timeline.copySelectedTrackToClipboard();

            const auto rowsBefore = mixManager.getMixTracks(mixId).size();
            if (!loader.reloadFromDatabase())
            {
                return stop("Could not reload the mix before the stale-paste check.");
            }

            timeline.pasteFromClipboard(false);
            const auto rowsAfterStalePaste = mixManager.getMixTracks(mixId).size();
            report.check(rowsAfterStalePaste == rowsBefore,
                std::format("a paste from views the loader has replaced is refused (rows {} -> {})", rowsBefore, rowsAfterStalePaste));

            // The other half of the same guard: it has to let a current timeline through, or it would
            // pass the check above by refusing everything.
            if (!timeline.populateFrom(&loader))
            {
                return stop("The timeline could not be repopulated after the refused paste.");
            }
            timeline.pasteFromClipboard(false);
            const auto rowsAfterFreshPaste = mixManager.getMixTracks(mixId).size();
            report.check(rowsAfterFreshPaste == rowsBefore + 1,
                std::format("the same paste goes through once the views are current again (rows {} -> {})", rowsBefore, rowsAfterFreshPaste));

            // The timeline is a local and takes its components with it; this drops the loader pointer
            // first so nothing outlives the loader either.
            timeline.releaseMixLoader();

            writeResultsFile(resultsPath, "jucyaudio timeline self test", report);
            spdlog::info("[SelfTest] Timeline test finished with {} failure(s). Results: {}", report.failures(), pathToString(resultsPath));
            return report.failures() == 0 ? 0 : 1;
        }

        int runFolderCacheSelfTest(const std::filesystem::path &selfTestRoot)
        {
            Report report;
            const auto resultsPath = selfTestRoot / "foldercache-results.txt";
            const auto workRoot = selfTestRoot / "foldercache-library";

            spdlog::info("[SelfTest] Starting folder cache self test. Root: {}", pathToString(selfTestRoot));

            auto &folders = theTrackLibrary.getTrackDatabase().getFolderDatabase();

            // Rows, not directories: findOrCreateFolderByPath writes the Folders table and never looks
            // at the disk. Enough of them that a cache build takes long enough to overlap with the
            // other thread - the window this test exists to enter is open only while one is running.
            constexpr int kSeedFolders = 400;
            for (int i = 0; i < kSeedFolders; ++i)
            {
                const auto path = workRoot / std::format("artist{:02}", i % 20) / std::format("album{:03}", i);
                if (folders.findOrCreateFolderByPath(path) <= 0)
                {
                    report.abort(std::format("Could not create the seed folder {}", pathToString(path)));
                    writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                    return 1;
                }
            }
            const auto rootFolderId = folders.findOrCreateFolderByPath(workRoot);
            if (rootFolderId <= 0)
            {
                report.abort("Could not create the folder the reader watches.");
                writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                return 1;
            }
            report.note(std::format("seeded {} folders under {} (folder id {})", kSeedFolders, pathToString(workRoot), rootFolderId));

            // The two paths that used to take the two mutexes in opposite orders. A reader forces cache
            // builds by invalidating between reads - buildCacheIfNeeded then holds the cache mutex and
            // reaches for the database mutex through its statements - while a writer runs
            // findOrCreateFolderByPath, which holds the database mutex and then wants the cache mutex.
            // Run against the inverted order these two stop each other dead; the test then fails by
            // deadline rather than by assertion, which is why there is one.
            constexpr int kIterations = 300;
            std::atomic<int> readsDone{0};
            std::atomic<int> writesDone{0};
            std::atomic<bool> readerFinished{false};
            std::atomic<bool> writerFinished{false};
            std::atomic<bool> invalidatorFinished{false};
            std::atomic<bool> stopInvalidating{false};
            std::atomic<int> readsThatLostTheRoot{0};
            std::atomic<int> creationsThatFailed{0};
            std::atomic<int> pathsThatChangedId{0};

            std::thread reader{[&]()
                {
                    for (int i = 0; i < kIterations; ++i)
                    {
                        folders.invalidateCache();
                        const auto seen = folders.getAllChildFolders({rootFolderId});
                        std::ignore = seen;
                        if (!folders.getFolderById(rootFolderId).has_value())
                        {
                            ++readsThatLostTheRoot;
                        }
                        ++readsDone;
                    }
                    readerFinished = true;
                }};

            // A third thread doing nothing but invalidating. The window that produced duplicate
            // folder rows is between the cache build inside findOrCreateFolderByPath and the lookup
            // that follows it, and it is a few microseconds wide: the reader above invalidates once
            // per pass and hit it about half the time. This one spins, which is not realistic use but
            // is what makes the check reliable rather than lucky.
            std::thread invalidator{[&]()
                {
                    while (!stopInvalidating)
                    {
                        folders.invalidateCache();
                    }
                    invalidatorFinished = true;
                }};

            std::thread writer{[&]()
                {
                    for (int i = 0; i < kIterations; ++i)
                    {
                        const auto path = workRoot / std::format("artist{:02}", i % 20) / std::format("live{:03}", i);
                        const auto id = folders.findOrCreateFolderByPath(path);
                        if (id <= 0)
                        {
                            ++creationsThatFailed;
                        }
                        else if (folders.findOrCreateFolderByPath(path) != id)
                        {
                            // The same path twice must be the same row, whatever the reader is doing to
                            // the cache in between.
                            ++pathsThatChangedId;
                        }
                        ++writesDone;
                    }
                    writerFinished = true;
                }};

            // Generous: this is a deadline for a hang, not a performance assertion. The work itself is
            // hundreds of small statements and takes a fraction of it.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{60};
            while ((!readerFinished || !writerFinished) && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds{20});
            }

            const bool finished = readerFinished && writerFinished;
            report.check(finished,
                std::format("the cache reader and the folder writer both finished ({}/{} reads, {}/{} writes)",
                    readsDone.load(),
                    kIterations,
                    writesDone.load(),
                    kIterations));

            // Told to stop either way, though on the failure path below nothing waits for it.
            stopInvalidating = true;

            if (!finished)
            {
                report.abort("Timed out. Two threads holding one mutex each and waiting for the other is what this looks like.");
                writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);

                // Out, without unwinding. There is no safe way back from here: a thread stuck on a mutex
                // never returns, so it can be neither joined nor left behind - joining hangs the run,
                // and returning destroys the state these threads hold references to and then tears down
                // the folder database they are standing in. The report is written and the log flushed
                // above, which is everything this run had to say.
                spdlog::error("[SelfTest] Folder cache test timed out; leaving the process without unwinding.");
                spdlog::default_logger()->flush();
                std::_Exit(1);
            }

            reader.join();
            writer.join();
            invalidator.join();
            report.check(invalidatorFinished, "the invalidator came back too");

            report.check(creationsThatFailed == 0,
                std::format("every folder the writer asked for was created ({} failed)", creationsThatFailed.load()));
            report.check(pathsThatChangedId == 0,
                std::format("the same path always came back as the same row ({} did not)", pathsThatChangedId.load()));
            // A count, not a check. An accessor builds the cache, releases both mutexes and then takes
            // the cache mutex to read, so the invalidator can empty it in between and the read misses -
            // the known window recorded in tasks.md, and not something this change set out to close.
            // Asserting zero here would fail a correct implementation on an unlucky schedule.
            report.note(std::format("{} of {} reads found the cache emptied under them, which is the known accessor window",
                readsThatLostTheRoot.load(),
                kIterations));

            // The cache has to agree with the database afterwards, or the locking is right and the
            // bookkeeping is not.
            folders.invalidateCache();
            const auto rebuiltCount = folders.getAllChildFolders({rootFolderId}).size();
            report.check(rebuiltCount > static_cast<size_t>(kSeedFolders),
                std::format("a rebuilt cache holds the seeded folders and the ones written during the run (found {})", rebuiltCount));


            // --- What removeEmptyFolders deletes, and what it must not ---
            //
            // A destructive path, and the one whose phases were rearranged to get the lock order right,
            // so it gets exercised rather than reasoned about. It deletes every folder with no tracks
            // under it, which is why it runs at the end: the folders seeded above are all empty.
            const auto usedRoot = selfTestRoot / "foldercache-used";
            const auto usedAlbum = usedRoot / "deep" / "album";
            std::error_code removalEc;
            std::filesystem::create_directories(usedAlbum, removalEc);
            if (removalEc)
            {
                report.abort(std::format("Could not create {}: {}", pathToString(usedAlbum), removalEc.message()));
                writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                return 1;
            }
            if (!writeSilentWav(usedAlbum / "keep.wav", static_cast<uint32_t>(44100 * kFixtureDurationMs / 1000)))
            {
                report.abort("Could not write the fixture the surviving folder is built on.");
                writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                return 1;
            }

            auto &trackDb = theTrackLibrary.getTrackDatabase();
            if (!trackDb.getLibraryRootManager().addRoot(pathToString(usedRoot)).has_value())
            {
                report.abort("Could not add the used-folder library as a root.");
                writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                return 1;
            }
            const auto usedRootId = folders.findOrCreateFolderByPath(usedRoot);
            if (usedRootId <= 0 || !runScan({usedRootId}, false, report, "used-folder discovery"))
            {
                report.abort("Could not discover the used-folder library.");
                writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                return 1;
            }

            // The three that must survive - the folder holding the track and both its ancestors - and
            // one sibling with nothing in it, which must not.
            const auto albumId = folders.findOrCreateFolderByPath(usedAlbum);
            const auto deepId = folders.findOrCreateFolderByPath(usedRoot / "deep");
            const auto emptySiblingId = folders.findOrCreateFolderByPath(usedRoot / "deep" / "nothing-here");
            if (albumId <= 0 || deepId <= 0 || emptySiblingId <= 0)
            {
                report.abort("Could not resolve the folders the removal check is about.");
                writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                return 1;
            }

            report.check(folders.removeEmptyFolders(), "removeEmptyFolders reports success");

            // By id, not by path: findOrCreateFolderByPath would put a deleted folder straight back.
            report.check(folders.getFolderById(albumId).has_value(),
                std::format("the folder holding a track survived (id {})", albumId));
            report.check(folders.getFolderById(deepId).has_value(),
                std::format("its ancestor survived (id {})", deepId));
            report.check(folders.getFolderById(usedRootId).has_value(),
                std::format("the library root above it survived (id {})", usedRootId));
            report.check(!folders.getFolderById(emptySiblingId).has_value(),
                std::format("the empty sibling is gone (id {})", emptySiblingId));
            report.check(!folders.getFolderById(rootFolderId).has_value(),
                std::format("so are the empty folders this suite seeded (id {})", rootFolderId));

            // --- A path the cache does not know, but the table does ---
            //
            // The schema says one Folders row per path since v31, so the insert findOrCreateFolderByPath
            // falls back on can now be refused - and it is refused exactly when the cache it just
            // consulted is out of date. Reporting that as "could not create the folder" would leave a
            // scan unable to place any track in the folder while the row it needed sat in the table.
            //
            // Staged over a second connection, because nothing reachable through this interface can
            // produce a cache that is valid and missing a row - which is the whole point of the index.
            // Its own database, since it deliberately writes behind a live cache's back.
            {
                const auto stagedDbPath = selfTestRoot / "foldercache-staged" / "jucyaudio.db";
                std::filesystem::remove_all(stagedDbPath.parent_path(), removalEc);
                std::filesystem::create_directories(stagedDbPath.parent_path(), removalEc);
                if (removalEc)
                {
                    report.abort(std::format("Could not create {}: {}", pathToString(stagedDbPath.parent_path()), removalEc.message()));
                    writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                    return 1;
                }

                SqliteTrackDatabase scratch;
                const auto connected = scratch.connect(stagedDbPath);
                report.check(connected.isOk(), std::format("a scratch database for the staged folder could be created (said: '{}')", connected.errorMessage));
                if (!connected.isOk())
                {
                    writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                    return 1;
                }

                auto &scratchFolders = scratch.getFolderDatabase();
                const auto knownPath = stagedDbPath.parent_path() / "library";
                const auto knownId = scratchFolders.findOrCreateFolderByPath(knownPath);
                report.check(knownId > 0, "the parent of the staged folder is in the cache");

                // A child of it, so the recursion in findOrCreateFolderByPath resolves the parent from
                // the cache and only the child itself reaches the insert.
                const auto stagedPath = knownPath / "staged";
                const auto stagedKey = normalizeForCache(pathToString(stagedPath));
                FolderId stagedId{-1};
                {
                    SqliteDatabase direct;
                    if (!direct.open(pathToString(stagedDbPath)))
                    {
                        report.abort("Could not reopen the scratch database to stage a folder behind its cache.");
                        writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                        return 1;
                    }

                    SqliteStatement insert{direct, "INSERT INTO Folders (parent_id, name, root_path, actual_path) VALUES (?, 'staged', ?, ?);"};
                    const bool staged = insert.isValid() && insert.addParam(knownId) && insert.addParam(stagedKey) &&
                        insert.addParam(pathToString(stagedPath)) && insert.execute();
                    report.check(staged, "a folder row could be written behind the live cache's back");
                    if (!staged)
                    {
                        writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                        return 1;
                    }
                    stagedId = direct.getLastInsertRowId();

                    // The index refuses a second row for that path, which is what makes the recovery
                    // below reachable in the first place.
                    SqliteStatement again{direct, "INSERT INTO Folders (parent_id, name, root_path) VALUES (?, 'staged-again', ?);"};
                    report.check(again.isValid() && again.addParam(knownId) && again.addParam(stagedKey) && !again.execute(),
                        "a second row for a path the table already has is refused");
                }

                report.check(scratchFolders.findOrCreateFolderByPath(stagedPath) == stagedId,
                    std::format("a folder the cache never saw comes back as the row that already exists (id {})", stagedId));
                report.check(scratchFolders.findOrCreateFolderByPath(stagedPath) == stagedId, "and the rebuilt cache agrees");
                report.check(scratchFolders.getFolderById(stagedId).has_value(), "the cache holds it by id too");
            }

            // --- A read that fails must delete nothing ---
            //
            // Everything removeEmptyFolders deletes is decided by what is *absent* from one read of
            // Tracks, so a read that fails or stops partway looks exactly like a library nobody uses.
            // Its own database, its own connection: this deliberately breaks the table the read needs,
            // and doing that to the library the other suites share would be a poor trade for a check.
            const auto brokenDbPath = selfTestRoot / "foldercache-broken" / "jucyaudio.db";
            std::filesystem::remove_all(brokenDbPath.parent_path(), removalEc);
            std::filesystem::create_directories(brokenDbPath.parent_path(), removalEc);
            if (removalEc)
            {
                report.abort(std::format("Could not create {}: {}", pathToString(brokenDbPath.parent_path()), removalEc.message()));
                writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                return 1;
            }

            {
                SqliteTrackDatabase scratch;
                const auto connected = scratch.connect(brokenDbPath);
                report.check(connected.isOk(), std::format("a scratch database could be created (said: '{}')", connected.errorMessage));
                if (!connected.isOk())
                {
                    writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                    return 1;
                }

                auto &scratchFolders = scratch.getFolderDatabase();
                std::vector<FolderId> seeded;
                for (int i = 0; i < 5; ++i)
                {
                    const auto id = scratchFolders.findOrCreateFolderByPath(brokenDbPath.parent_path() / std::format("empty{:02}", i));
                    if (id <= 0)
                    {
                        report.abort("Could not seed the scratch database with folders.");
                        writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                        return 1;
                    }
                    seeded.push_back(id);
                }

                // Counted in the database rather than through the cache. With a broken Tracks the cache
                // cannot be rebuilt at all, so a cache-based count would drop to zero whether the rows
                // were deleted or not - it would report a refusal that never happened as a success.
                const auto countFolderRows = [&brokenDbPath, &report]() -> int64_t
                {
                    SqliteDatabase counter;
                    if (!counter.open(pathToString(brokenDbPath)))
                    {
                        report.abort("Could not reopen the scratch database to count its folders.");
                        return -1;
                    }
                    SqliteStatement stmt{counter, "SELECT COUNT(*) FROM Folders"};
                    return (stmt.isValid() && stmt.getNextResult()) ? stmt.getInt64(0) : -1;
                };

                // Built while Tracks still answers, and not invalidated afterwards: removeEmptyFolders
                // takes its list of candidates from the cache as it finds it, so an empty cache would
                // leave it nothing to delete and every check below would pass without proving anything.
                report.check(scratchFolders.getFolderById(seeded.front()).has_value(), "the scratch folder cache is built and holds the seeded folders");

                const auto folderRowsBefore = countFolderRows();
                report.check(folderRowsBefore >= static_cast<int64_t>(seeded.size()),
                    std::format("the scratch database holds the {} empty folders and their parents ({} rows)", seeded.size(), folderRowsBefore));

                // A Tracks that answers one row and then fails, over a second connection so the schema
                // change is committed before the folder database reads it. abs() of the most negative
                // integer is a runtime error in SQLite, not a parse error, so the failure lands in the
                // middle of the read rather than when it is prepared - the partial read this is about.
                {
                    SqliteDatabase saboteur;
                    if (!saboteur.open(pathToString(brokenDbPath)))
                    {
                        report.abort("Could not reopen the scratch database to break its Tracks table.");
                        writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                        return 1;
                    }
                    const bool broken = saboteur.execute("DROP TABLE Tracks;") &&
                        saboteur.execute("CREATE VIEW Tracks (folder_id, track_id) AS "
                                         "SELECT 1, 1 UNION ALL SELECT abs(-9223372036854775807 - 1), 2;");
                    report.check(broken, "the scratch Tracks was replaced by one that fails halfway through a read");
                    if (!broken)
                    {
                        writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                        return 1;
                    }
                }

                report.check(!scratchFolders.removeEmptyFolders(), "removeEmptyFolders refuses when the read of the folders in use fails");
                report.check(countFolderRows() == folderRowsBefore,
                    std::format("it deleted no folder it could not prove was unused ({} rows before, {} after)",
                        folderRowsBefore,
                        countFolderRows()));

                // And a Tracks that is not there at all. Worth its own case even though it lands in the
                // same check: sqlite3_prepare_v2 re-prepares a statement when the schema has changed, so
                // a table that has since been dropped is reported when the statement is stepped and not
                // when it is prepared. Reading the missing table is a runtime failure, not a syntax one.
                {
                    SqliteDatabase saboteur;
                    if (!saboteur.open(pathToString(brokenDbPath)) || !saboteur.execute("DROP VIEW Tracks;"))
                    {
                        report.abort("Could not remove the scratch Tracks view.");
                        writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
                        return 1;
                    }
                }

                report.check(!scratchFolders.removeEmptyFolders(), "removeEmptyFolders refuses when the table it reads is gone");
                report.check(countFolderRows() == folderRowsBefore,
                    std::format("it deleted nothing then either ({} rows)", countFolderRows()));
            }

            writeResultsFile(resultsPath, "jucyaudio folder cache self test", report);
            spdlog::info("[SelfTest] Folder cache test finished with {} failure(s). Results: {}", report.failures(), pathToString(resultsPath));
            return report.failures() == 0 ? 0 : 1;
        }

    } // namespace tests
} // namespace jucyaudio
