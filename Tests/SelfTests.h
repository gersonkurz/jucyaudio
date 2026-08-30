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

#include <filesystem>
#include <string>

namespace jucyaudio
{
    namespace tests
    {
        /**
         * @brief Establishes that it is safe to run the self test, and says where its files go.
         *
         * Must be called before the database is opened. Opening one runs schema migrations, so by the
         * time the test itself could object it would already have written to whatever it was pointed at.
         *
         * Isolation does not rely on the config root at all. It is decided by JUCYAUDIO_SELFTEST_ROOT, a
         * variable that exists for no other purpose, so setting it is a statement of intent rather than
         * a side effect of some other configuration. The directory it names must additionally be empty
         * or carry this test's sentinel file, which stops the test from emptying a populated directory
         * someone aimed it at by mistake. Comparing paths against the default config root was the
         * previous approach and was not enough: a developer running a permanent custom config root
         * passes such a check, and the comparison is lexical, so equivalent spellings slip past it.
         *
         * The database is a dedicated file inside that root, never the configured one, and any stale
         * copy is deleted so each run starts from an empty library.
         *
         * @param rootOut Receives the self test's working root. Untouched on failure.
         * @param databasePathOut Receives the self test's own database path. Untouched on failure.
         * @return An empty string if the test may run, otherwise the reason it may not.
         */
        std::string prepareSelfTestEnvironment(std::filesystem::path &rootOut, std::filesystem::path &databasePathOut);

        /**
         * @brief Headless end-to-end test of the library scanner's missing-file handling.
         *
         * Drives a real scan against a throwaway library it builds itself: a track goes missing when
         * its folder disappears, stays missing across a repeat scan, and stops being missing when the
         * folder comes back - keeping the track_id it always had, and with it the mix and working set
         * rows that reference it.
         *
         * Nothing here touches the UI. TrackLibrary::scanLibrary() needs an open database and nothing
         * else, so the whole cycle runs on the message thread before any window exists.
         *
         * @param selfTestRoot The root returned by prepareSelfTestEnvironment(). The scratch library and
         *        the results file are created underneath it.
         * @return 0 if every check passed, 1 otherwise. Suitable as a process exit code.
         */
        /// @param databasePath The same scratch database, so one check can stage a state the public
        ///        interfaces deliberately cannot produce: a mix whose stored summary disagrees with its
        ///        rows. Nothing can write those columns directly any more, which is the fix - and also
        ///        the reason the repair that corrects them has to be exercised from outside.
        int runScanSelfTest(const std::filesystem::path &selfTestRoot, const std::filesystem::path &databasePath);

        /**
         * @brief Headless end-to-end test of mix recovery data.
         *
         * Exports a scratch mix through the real exporter, then checks that what was recorded matches
         * what was exported, that a mix which has lost rows cannot overwrite its own record, and that
         * the record outlives the tracks but not the mix.
         *
         * That last pair is the point. MixRecovery carries a foreign key on mix_id and deliberately none
         * on track_id, so deleting a track leaves the record standing while deleting the mix takes it
         * away. Asserting only one half would pass with the keys the wrong way round, so both are here -
         * this is the test that should fail if someone ever "fixes" the schema by adding the missing
         * foreign key.
         *
         * Uses its own scratch library, separate from the scan test's, because it deletes tracks and
         * mixes as part of what it asserts.
         *
         * @param selfTestRoot The root returned by prepareSelfTestEnvironment().
         * @param databasePath The same scratch database, so one check can stage a fixture over its own
         *        short-lived connection: an unknown JSON field written straight into MixTracks, which
         *        nothing reachable through the public interfaces can produce. Everything asserted about
         *        it afterwards goes through those interfaces.
         * @return 0 if every check passed, 1 otherwise.
         */
        int runMixRecoverySelfTest(const std::filesystem::path &selfTestRoot, const std::filesystem::path &databasePath);

        /**
         * @brief Headless test that a database backup is actually a backup.
         *
         * Builds a scratch database, commits data into it **without checkpointing** so those pages live
         * only in the -wal file, backs it up, and then opens the backup and looks for them.
         *
         * That is the whole test, and it is the one that matters: a backup taken by copying the main
         * database file passes every casual inspection - right size, opens fine, reports success - and
         * silently omits everything sitting in the WAL. It fails this. An online backup passes.
         *
         * Uses its own database, unrelated to the one the other suites share, because it needs to
         * control checkpointing.
         *
         * @param selfTestRoot The root returned by prepareSelfTestEnvironment().
         * @return 0 if every check passed, 1 otherwise.
         */
        int runBackupSelfTest(const std::filesystem::path &selfTestRoot);

    } // namespace tests
} // namespace jucyaudio
