#include "DatabaseBackupTask.h"
#include <Database/DatabaseBackupManager.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        DatabaseBackupTask::DatabaseBackupTask(const config::RootSettings& settings)
            : IBackgroundTask("DatabaseBackup"), m_settings(settings)
        {
        }

        void DatabaseBackupTask::processWork()
        {
            // This task should only execute once at application startup.
            if (m_hasRun)
            {
                return;
            }

            spdlog::info("Starting one-time database backup check...");
            
            DatabaseBackupManager backupManager;
            // Initially, run in dry-run mode for safety.
            // This can be changed to 'false' after testing is complete.
            backupManager.performBackupCheck(m_settings, true);

            m_hasRun = true;
            spdlog::info("One-time database backup check finished.");
        }
    } // namespace database
} // namespace jucyaudio
