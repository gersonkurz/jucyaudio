#pragma once

#include <Database/Includes/IBackgroundTask.h>
#include <UI/Settings.h>

namespace jucyaudio
{
    namespace database
    {
        class DatabaseBackupTask final : public IBackgroundTask
        {
        public:
            DatabaseBackupTask(const config::RootSettings& settings);
            ~DatabaseBackupTask() override = default;

            void processWork() override;

        private:
            const config::RootSettings& m_settings;
            bool m_hasRun = false; // Ensure the task only runs once on startup
        };
    } // namespace database
} // namespace jucyaudio
