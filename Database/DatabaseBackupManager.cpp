#include "DatabaseBackupManager.h"
#include <Database/Sqlite/sqlite3.h>
#include <UI/Settings.h>
#include <Utils/AssortedUtils.h>
#include <spdlog/spdlog.h>
#include <regex>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <format>
#include <random>
#include <system_error>

namespace jucyaudio
{
    namespace database
    {
        BackupOutcome DatabaseBackupManager::performBackupCheck(const config::RootSettings& appSettings, const std::filesystem::path& databaseFile, bool dryRunCreation, bool dryRunPruning, bool forceCreation)
        {
            spdlog::info("[Backup Manager] Starting backup check. Creation Dry Run: {}, Pruning Dry Run: {}, Force Creation: {}", dryRunCreation, dryRunPruning, forceCreation);

            BackupOutcome outcome;
            try
            {
                if (!std::filesystem::exists(databaseFile))
                {
                    // Not a failure: a database that does not exist yet is about to be created, and there
                    // is nothing to protect. Reported as "not attempted" rather than "failed" so a caller
                    // requiring a backup can tell the two apart.
                    spdlog::warn("[Backup Manager] Source database file does not exist, skipping backup: {}", pathToString(databaseFile));
                    return outcome;
                }

                const auto dbDirectory = databaseFile.parent_path();
                auto existingBackups = getExistingBackups(dbDirectory);
                spdlog::info("[Backup Manager] Found {} existing backups.", existingBackups.size());

                if (forceCreation || isBackupNeeded(existingBackups))
                {
                    spdlog::info("[Backup Manager] {} Proceeding to create a new backup.",
                        forceCreation ? "Forced backup requested." : "Backup is needed.");
                    outcome = createNewBackup(databaseFile, existingBackups, dryRunCreation);
                    // After creating a new backup, we need to refresh the list before pruning.
                    if (!dryRunCreation)
                    {
                        existingBackups = getExistingBackups(dbDirectory);
                    }
                }
                else
                {
                    spdlog::info("[Backup Manager] No new backup needed at this time.");
                }

                pruneOldBackups(existingBackups, appSettings.backupSettings.numberOfBackups, dryRunPruning);
            }
            catch (const std::exception& e)
            {
                spdlog::error("[Backup Manager] An exception occurred during backup check: {}", e.what());
                outcome.succeeded = false;
                outcome.errorMessage = e.what();
            }
            spdlog::info("[Backup Manager] Backup check finished.");
            return outcome;
        }

        std::vector<DatabaseBackupManager::BackupInfo> DatabaseBackupManager::getExistingBackups(const std::filesystem::path& dbDirectory) const
        {
            std::vector<BackupInfo> backups;
            const std::regex backupRegex(R"((\d{2})-(\d{4}-\d{2}-\d{2})\.sqlite)");

            for (const auto& entry : std::filesystem::directory_iterator(dbDirectory))
            {
                if (entry.is_regular_file())
                {
                    // pathToString, not path::string(): this runs over every sibling of the database,
                    // and one non-ASCII filename beside it would throw and abort the whole backup check.
                    const std::string filename = pathToString(entry.path().filename());
                    if (std::regex_match(filename, backupRegex))
                    {
                        if (auto backupInfo = parseBackupFilename(entry.path()))
                        {
                            backups.push_back(*backupInfo);
                        }
                    }
                }
            }

            std::sort(backups.begin(), backups.end());
            return backups;
        }

        std::optional<DatabaseBackupManager::BackupInfo> DatabaseBackupManager::parseBackupFilename(const std::filesystem::path& backupFile) const
        {
            const std::string filename = pathToString(backupFile.filename());
            const std::regex backupRegex(R"((\d{2})-(\d{4}-\d{2}-\d{2})\.sqlite)");
            std::smatch matches;

            if (std::regex_match(filename, matches, backupRegex) && matches.size() == 3)
            {
                try
                {
                    int sequenceNumber = std::stoi(matches[1].str());
                    std::string dateStr = matches[2].str();
                    
                    std::tm tm = {};
                    std::stringstream ss(dateStr);
                    ss >> std::get_time(&tm, "%Y-%m-%d");
                    if (ss.fail()) {
                        return std::nullopt;
                    }
                    
                    auto timePoint = std::chrono::system_clock::from_time_t(std::mktime(&tm));

                    return BackupInfo{backupFile, sequenceNumber, timePoint};
                }
                catch (const std::exception& e)
                {
                    spdlog::warn("[Backup Manager] Could not parse backup filename '{}': {}", filename, e.what());
                    return std::nullopt;
                }
            }
            
            return std::nullopt;
        }

        bool DatabaseBackupManager::isBackupNeeded(const std::vector<BackupInfo>& backups) const
        {
            if (backups.empty())
            {
                return true;
            }

            const auto& newestBackup = backups.front();
            const auto now = std::chrono::system_clock::now();
            const auto age = now - newestBackup.creationTime;

            return age > std::chrono::days(7);
        }

        BackupOutcome DatabaseBackupManager::createNewBackup(const std::filesystem::path& sourceDbFile, const std::vector<BackupInfo>& existingBackups, bool dryRun)
        {
            BackupOutcome outcome;
            outcome.attempted = true;

            int nextSequenceNumber = 1;
            if (!existingBackups.empty())
            {
                nextSequenceNumber = existingBackups.front().sequenceNumber + 1;
            }

            const auto now = std::chrono::system_clock::now();
            const auto dateString = std::format("{:%Y-%m-%d}", now);
            const auto newFilename = std::format("{:02d}-{}.sqlite", nextSequenceNumber, dateString);
            const auto newBackupFile = sourceDbFile.parent_path() / newFilename;
            outcome.backupFile = newBackupFile;

            if (dryRun)
            {
                spdlog::info("[Backup Manager Dry Run] Would create new backup: {}", pathToString(newBackupFile));
                return outcome;
            }

            spdlog::info("[Backup Manager] Creating new backup: {}", pathToString(newBackupFile));

            // Written to a temporary and moved into place only once it is complete. A partial file
            // sitting under a name that looks like a backup is worse than no backup at all, because the
            // next run will count it, prune against it, and one day restore from it.
            //
            // The temporary name is unique per attempt, because the application permits multiple
            // instances and two of them will pick the same sequence number on the same day. With one
            // shared name, one instance can delete the other's temporary mid-write and then rename the
            // survivor's half-finished file to the final backup name, reporting success. So each attempt
            // gets a name nothing else will choose, and no temporary is ever pre-emptively removed.
            //
            // A clock reading plus a random token rather than a process id: there is no portable way to
            // ask for one, and this needs no platform branches to be at least as unlikely to collide.
            //
            // The final name is claimed separately, further down, and a collision there costs nothing:
            // the loser simply takes the next sequence number and both backups survive.
            const auto uniqueSuffix = std::format("{}-{:08x}",
                std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count(),
                std::random_device{}());
            const auto tempFile = sourceDbFile.parent_path() / std::format("{:02d}-{}.{}.partial", nextSequenceNumber, dateString, uniqueSuffix);
            std::error_code ec;

            const auto fail = [&outcome, &tempFile](std::string why) -> BackupOutcome
            {
                std::error_code removeEc;
                std::filesystem::remove(tempFile, removeEc);
                spdlog::error("[Backup Manager] {}", why);
                outcome.succeeded = false;
                // Cleared: it held the name this was aiming for, and leaving it set on a failure hands
                // the caller a path to a file that does not exist.
                outcome.backupFile.clear();
                outcome.errorMessage = std::move(why);
                return outcome;
            };

            // SQLite's own backup API rather than copying the file. The database runs in WAL mode, so
            // pages that are committed but not yet checkpointed live in the -wal file, and copying only
            // the main file can miss them - producing something that looks like a backup, reports
            // success, and restores torn. Multiple instances are permitted too, so a plain copy can be
            // taken while another one is mid-write. The backup API reads a consistent snapshot of a live
            // database, WAL content included, without stopping anyone.
            sqlite3 *source = nullptr;
            if (sqlite3_open_v2(pathToString(sourceDbFile).c_str(), &source, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
            {
                const std::string why = std::format("Could not open the database to back it up: {}", source ? sqlite3_errmsg(source) : "unknown error");
                sqlite3_close(source);
                return fail(why);
            }

            sqlite3 *destination = nullptr;
            if (sqlite3_open_v2(pathToString(tempFile).c_str(), &destination, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
            {
                const std::string why = std::format("Could not create the backup file: {}", destination ? sqlite3_errmsg(destination) : "unknown error");
                sqlite3_close(destination);
                sqlite3_close(source);
                return fail(why);
            }

            // A short wait inside SQLite before it reports a lock as busy, so brief contention from
            // another instance is absorbed here rather than surfacing as a retry above.
            constexpr int kBusyTimeoutMs = 5000;
            sqlite3_busy_timeout(source, kBusyTimeoutMs);
            sqlite3_busy_timeout(destination, kBusyTimeoutMs);

            auto *backup = sqlite3_backup_init(destination, "main", source, "main");
            if (backup == nullptr)
            {
                const std::string why = std::format("Could not start the backup: {}", sqlite3_errmsg(destination));
                sqlite3_close(destination);
                sqlite3_close(source);
                return fail(why);
            }

            // -1 copies everything in one call. This runs before the UI exists, so there is nobody to
            // keep responsive and nothing gained by stepping in pages.
            //
            // Retried on SQLITE_BUSY and SQLITE_LOCKED, which the backup API defines as retryable rather
            // than fatal: another instance writing to the source can hold it off, and losing the whole
            // backup cycle over a moment's contention is the wrong trade when multiple instances are a
            // deliberate part of how this application runs.
            //
            // Bounded by one wall-clock deadline rather than a retry count. A count would multiply
            // against the busy timeout above - each step can itself block for that long before reporting
            // busy - so "forty retries" would have meant minutes, not the seconds it reads like. The
            // deadline is what was actually intended, and it says so directly.
            constexpr auto kBusyDeadline = std::chrono::seconds{30};
            constexpr int kBusyRetryPauseMs = 250;
            const auto giveUpAt = std::chrono::steady_clock::now() + kBusyDeadline;

            int stepResult = SQLITE_OK;
            int attempts = 0;
            while (true)
            {
                ++attempts;
                stepResult = sqlite3_backup_step(backup, -1);
                if (stepResult != SQLITE_BUSY && stepResult != SQLITE_LOCKED)
                {
                    break;
                }

                // Checked before sleeping, so the last thing this loop does is an attempt rather than a
                // pointless wait after one.
                if (std::chrono::steady_clock::now() >= giveUpAt)
                {
                    spdlog::warn("[Backup Manager] Source database still busy after {} attempts; giving up on this backup.", attempts);
                    break;
                }

                spdlog::warn("[Backup Manager] Source database is busy, retrying (attempt {}).", attempts);
                sqlite3_sleep(kBusyRetryPauseMs);
            }

            const int finishResult = sqlite3_backup_finish(backup);
            const std::string destinationError = sqlite3_errmsg(destination);
            sqlite3_close(destination);
            sqlite3_close(source);

            // Both must agree. step says the copy ran to the end, finish says it was committed and
            // closed cleanly; publishing on either alone would put an incomplete file in place.
            if (stepResult != SQLITE_DONE || finishResult != SQLITE_OK)
            {
                return fail(std::format("The backup did not complete (step {}, finish {}): {}", stepResult, finishResult, destinationError));
            }

            // Claim a free final name rather than replacing whatever is there, so two instances finishing
            // together keep both backups instead of one quietly discarding the other.
            //
            // Claimed with a hard link, not exists() followed by rename(). Testing for absence and then
            // renaming is a race with a window between the two, and POSIX rename replaces the
            // destination without complaint - so both instances would see a free name and the second
            // would overwrite the first, both reporting success. create_hard_link fails when the target
            // exists, atomically, which is the one operation that actually reserves a name. The
            // temporary is then unlinked, leaving exactly one file under the claimed name.
            constexpr int kMaxSequenceProbes = 20;
            bool placed = false;
            auto finalFile = newBackupFile;

            for (int probe = 0; probe < kMaxSequenceProbes && !placed; ++probe)
            {
                const auto candidate = sourceDbFile.parent_path() / std::format("{:02d}-{}.sqlite", nextSequenceNumber + probe, dateString);

                std::error_code linkEc;
                std::filesystem::create_hard_link(tempFile, candidate, linkEc);
                if (!linkEc)
                {
                    finalFile = candidate;
                    placed = true;

                    // The backup is published at this point - the link is the real file, and it is
                    // complete. Failing to unlink the temporary afterwards does not undo any of that, so
                    // it must not be reported as a failed backup, and the good link is certainly not
                    // deleted to tidy up. But it is not nothing either: a .partial left beside the
                    // backups is exactly what the temporary scheme exists to avoid, so it is said out
                    // loud rather than swallowed.
                    std::error_code removeEc;
                    std::filesystem::remove(tempFile, removeEc);
                    if (removeEc)
                    {
                        outcome.warningMessage =
                            std::format("The backup was written, but its temporary file {} could not be removed: {}", pathToString(tempFile), removeEc.message());
                        spdlog::warn("[Backup Manager] {}", outcome.warningMessage);
                    }
                    break;
                }

                if (linkEc == std::errc::file_exists)
                {
                    continue; // somebody has that number; take the next one
                }

                // Hard links are not available here - some filesystems have none. Fall back to a plain
                // rename, which is what this did before and is still correct when nothing else is
                // running. Recorded rather than silent, because it is the one path where two instances
                // finishing together can still cost a backup.
                spdlog::warn("[Backup Manager] Could not claim {} by hard link ({}); falling back to rename.", pathToString(candidate), linkEc.message());

                std::error_code renameEc;
                std::filesystem::rename(tempFile, candidate, renameEc);
                if (!renameEc)
                {
                    finalFile = candidate;
                    placed = true;
                }
                break;
            }

            if (!placed)
            {
                return fail(std::format("Could not put the finished backup in place near {}", pathToString(newBackupFile)));
            }

            outcome.backupFile = finalFile;
            spdlog::info("[Backup Manager] Backup complete: {}", pathToString(finalFile));
            outcome.succeeded = true;
            return outcome;
        }

        void DatabaseBackupManager::pruneOldBackups(const std::vector<BackupInfo>& backups, int maxBackups, bool dryRun)
        {
            if (backups.size() <= static_cast<size_t>(maxBackups))
            {
                return;
            }

            spdlog::info("[Backup Manager] Pruning old backups. Found {}, keeping {}.", backups.size(), maxBackups);

            for (size_t i = maxBackups; i < backups.size(); ++i)
            {
                const auto& backupToPrune = backups[i];
                if (dryRun)
                {
                    spdlog::info("[Backup Manager Dry Run] Would delete old backup: {}", pathToString(backupToPrune.path));
                }
                else
                {
                    spdlog::info("[Backup Manager] Deleting old backup: {}", pathToString(backupToPrune.path));
                    try
                    {
                        if (!std::filesystem::remove(backupToPrune.path))
                        {
                            spdlog::error("[Backup Manager] Failed to delete old backup file.");
                        }
                    }
                    catch (const std::filesystem::filesystem_error& e)
                    {
                        spdlog::error("[Backup Manager] Error deleting old backup file: {}", e.what());
                    }
                }
            }
        }
    } // namespace database
} // namespace jucyaudio