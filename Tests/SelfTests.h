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

        /**
         * @brief Headless test of the v29 to v30 MixRecovery migration.
         *
         * Builds a database shaped the way v29 left one - by hand, because nothing produces that shape
         * any more - seeds it with an intact record and a gapped partial one, opens it so the ladder
         * runs, and checks what came out.
         *
         * That migration gets a suite to itself because it is the only one in the project that rewrites
         * primary keys, and it does so to records that cannot be recaptured: the mixes they describe
         * have already lost the rows in question, and no backup holds them. Getting the renumbering
         * wrong would not fail loudly - it would quietly reorder somebody's last description of a mix.
         *
         * Uses its own database, unrelated to the one the other suites share.
         *
         * @param selfTestRoot The root returned by prepareSelfTestEnvironment().
         * @return 0 if every check passed, 1 otherwise.
         */
        int runMigrationSelfTest(const std::filesystem::path &selfTestRoot);

        /**
         * @brief Headless test that the mix editor timeline survives a reload it was not told about.
         *
         * Builds a scratch mix, populates a real TimelineComponent from a real MixProjectLoader, and
         * then reloads the loader behind the timeline - which is what any other view of the same mix
         * does when it calls refreshCache(true). The timeline used to hold raw pointers into the two
         * vectors that reload replaces, so its next layout pass, paint or drag read freed memory.
         *
         * Two things are asserted, because the fix has two halves. The views own their data, so the
         * layout pass after such a reload completes - no test can prove the absence of a
         * use-after-free, but this is the walk that used to commit one. And an edit made from views
         * the loader has replaced is refused rather than written: the positions on screen name rows
         * that now belong to other tracks, and isLoaded() cannot say so because the reload succeeded.
         * The same edit is then repeated on a repopulated timeline and must go through, so that a
         * guard which simply refuses everything fails this too.
         *
         * Constructs UI components, which is why it is the only suite here that does. It creates no
         * window and pumps no messages - a JUCE component needs only the GUI machinery the
         * application has already initialised by this point.
         *
         * Uses its own scratch library, because it writes to the mix it builds.
         *
         * @param selfTestRoot The root returned by prepareSelfTestEnvironment().
         * @return 0 if every check passed, 1 otherwise.
         */
        int runTimelineSelfTest(const std::filesystem::path &selfTestRoot);

        /**
         * @brief Headless test that the folder cache and the folder writer do not stop each other.
         *
         * SqliteFolderDatabase guards its four lookup maps with one mutex and reaches the database
         * through another. Both are needed on two paths, and they used to be taken in opposite
         * orders: buildCacheIfNeeded held the cache mutex and then acquired the database mutex
         * through its statements, while findOrCreateFolderByPath held the database mutex and then
         * wanted the cache mutex. Two threads, one on each path, could hold one each and wait
         * forever - narrowest during a scan, which is when both run hardest.
         *
         * So this suite runs exactly those two paths against each other: a reader that invalidates
         * the cache between reads, forcing builds, and a writer creating folders. A failure here is
         * a hang rather than a wrong answer, so the wait has a deadline and the report says which
         * one it was. It also checks the answers, because locking that is correct and bookkeeping
         * that is not look the same from outside.
         *
         * @param selfTestRoot The root returned by prepareSelfTestEnvironment().
         * @return 0 if every check passed, 1 otherwise.
         */
        int runFolderCacheSelfTest(const std::filesystem::path &selfTestRoot);

    } // namespace tests
} // namespace jucyaudio
