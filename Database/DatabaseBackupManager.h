#pragma once

#include <filesystem>
#include <chrono>
#include <vector>
#include <string>
#include <optional>

// Forward-declare the Settings class to avoid including the full header
namespace jucyaudio { namespace config { class RootSettings; } }

namespace jucyaudio
{
    namespace database
    {
        class DatabaseBackupManager final
        {
        public:
            DatabaseBackupManager() = default;
            ~DatabaseBackupManager() = default;

            /**
             * @brief Checks if a database backup is needed and performs the backup and pruning process.
             * 
             * @param appSettings The application's root settings object.
             * @param databaseFile The full path to the database file to be backed up.
             * @param dryRunCreation If true, backup file creation will be logged but not performed.
             * @param dryRunPruning If true, old backup file deletion will be logged but not performed.
             */
            void performBackupCheck(const config::RootSettings& appSettings, const std::filesystem::path& databaseFile, bool dryRunCreation, bool dryRunPruning);

        private:
            /**
             * @brief Represents a parsed backup file, containing its path, sequence number, and creation time.
             */
            struct BackupInfo
            {
                std::filesystem::path path;
                int sequenceNumber;
                std::chrono::system_clock::time_point creationTime;

                // Comparison operator to allow sorting from newest to oldest
                bool operator<(const BackupInfo& other) const { return creationTime > other.creationTime; }
            };

            std::vector<BackupInfo> getExistingBackups(const std::filesystem::path& dbDirectory) const;
            void pruneOldBackups(const std::vector<BackupInfo>& backups, int maxBackups, bool dryRun);
            bool isBackupNeeded(const std::vector<BackupInfo>& backups) const;
            void createNewBackup(const std::filesystem::path& sourceDbFile, const std::vector<BackupInfo>& existingBackups, bool dryRun);
            std::optional<BackupInfo> parseBackupFilename(const std::filesystem::path& backupFile) const;
        };
    } // namespace database
} // namespace jucyaudio