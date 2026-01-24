#include "DatabaseBackupManager.h"
#include <UI/Settings.h>
#include <spdlog/spdlog.h>
#include <regex>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <format>

namespace jucyaudio
{
    namespace database
    {
        void DatabaseBackupManager::performBackupCheck(const config::RootSettings& appSettings, const std::filesystem::path& databaseFile, bool dryRunCreation, bool dryRunPruning, bool forceCreation)
        {
            spdlog::info("[Backup Manager] Starting backup check. Creation Dry Run: {}, Pruning Dry Run: {}, Force Creation: {}", dryRunCreation, dryRunPruning, forceCreation);

            try
            {
                if (!std::filesystem::exists(databaseFile))
                {
                    spdlog::warn("[Backup Manager] Source database file does not exist, skipping backup: {}", databaseFile.string());
                    return;
                }

                const auto dbDirectory = databaseFile.parent_path();
                auto existingBackups = getExistingBackups(dbDirectory);
                spdlog::info("[Backup Manager] Found {} existing backups.", existingBackups.size());

                if (forceCreation || isBackupNeeded(existingBackups))
                {
                    spdlog::info("[Backup Manager] {} Proceeding to create a new backup.",
                        forceCreation ? "Forced backup requested." : "Backup is needed.");
                    createNewBackup(databaseFile, existingBackups, dryRunCreation);
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
            }
            spdlog::info("[Backup Manager] Backup check finished.");
        }

        std::vector<DatabaseBackupManager::BackupInfo> DatabaseBackupManager::getExistingBackups(const std::filesystem::path& dbDirectory) const
        {
            std::vector<BackupInfo> backups;
            const std::regex backupRegex(R"((\d{2})-(\d{4}-\d{2}-\d{2})\.sqlite)");

            for (const auto& entry : std::filesystem::directory_iterator(dbDirectory))
            {
                if (entry.is_regular_file())
                {
                    const std::string filename = entry.path().filename().string();
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
            const std::string filename = backupFile.filename().string();
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

        void DatabaseBackupManager::createNewBackup(const std::filesystem::path& sourceDbFile, const std::vector<BackupInfo>& existingBackups, bool dryRun)
        {
            int nextSequenceNumber = 1;
            if (!existingBackups.empty())
            {
                nextSequenceNumber = existingBackups.front().sequenceNumber + 1;
            }

            const auto now = std::chrono::system_clock::now();
            const auto dateString = std::format("{:%Y-%m-%d}", now);
            const auto newFilename = std::format("{:02d}-{}.sqlite", nextSequenceNumber, dateString);
            const auto newBackupFile = sourceDbFile.parent_path() / newFilename;

            if (dryRun)
            {
                spdlog::info("[Backup Manager Dry Run] Would create new backup: {}", newBackupFile.string());
            }
            else
            {
                spdlog::info("[Backup Manager] Creating new backup: {}", newBackupFile.string());
                try
                {
                    std::filesystem::copy_file(sourceDbFile, newBackupFile);
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    spdlog::error("[Backup Manager] Failed to create backup file: {}", e.what());
                }
            }
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
                    spdlog::info("[Backup Manager Dry Run] Would delete old backup: {}", backupToPrune.path.string());
                }
                else
                {
                    spdlog::info("[Backup Manager] Deleting old backup: {}", backupToPrune.path.string());
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