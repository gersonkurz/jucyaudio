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
        /**
         * @brief What a backup check actually did.
         *
         * Startup does not care and can keep ignoring it. Anything about to modify the database
         * structurally does: "we tried" is not a basis for running a schema migration against a
         * multi-gigabyte library.
         */
        struct BackupOutcome final
        {
            /// @brief Whether a backup was called for at all. False when a recent one already exists.
            bool attempted{false};

            /// @brief Whether a complete, consistent backup now exists on disk. Only meaningful when
            /// attempted; a caller needing certainty should force creation and require this.
            bool succeeded{false};

            /// @brief Where it went, when it succeeded.
            std::filesystem::path backupFile;

            /// @brief Why it did not, when it failed.
            std::string errorMessage;

            /**
             * @brief Something worth knowing about a backup that nonetheless succeeded.
             *
             * Separate from errorMessage, because the two call for different responses. The backup at
             * backupFile is complete and usable; this says the housekeeping around it was not - a
             * temporary left behind, most likely, because something held a handle on it. A caller that
             * requires a backup before doing something dangerous should proceed; a caller tidying up
             * should look.
             */
            std::string warningMessage;
        };

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
             * @param forceCreation If true, create a backup regardless of whether one is needed.
             * @return What happened. Callers that only want the housekeeping may ignore it.
             */
            BackupOutcome performBackupCheck(const config::RootSettings& appSettings, const std::filesystem::path& databaseFile, bool dryRunCreation, bool dryRunPruning, bool forceCreation = false);

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
            BackupOutcome createNewBackup(const std::filesystem::path& sourceDbFile, const std::vector<BackupInfo>& existingBackups, bool dryRun);
            std::optional<BackupInfo> parseBackupFilename(const std::filesystem::path& backupFile) const;
        };
    } // namespace database
} // namespace jucyaudio