#pragma once

#include <Database/Includes/IReverbPresetManager.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <Database/Sqlite/SqliteStatement.h>

namespace jucyaudio
{
    namespace database
    {
        /**
         * SQLite implementation of the reverb preset manager
         */
        class SqliteReverbPresetManager : public IReverbPresetManager
        {
        public:
            explicit SqliteReverbPresetManager(SqliteDatabase &db);
            ~SqliteReverbPresetManager() override;

            // IReverbPresetManager implementation
            std::vector<model::ReverbPreset> getAllPresets() override;
            std::optional<model::ReverbPreset> getPreset(int64_t presetId) override;
            std::optional<model::ReverbPreset> savePreset(const std::string &name, const audio::model::ReverbSettings &settings) override;
            bool deletePreset(int64_t presetId) override;
            bool updatePreset(int64_t presetId, const audio::model::ReverbSettings &settings) override;
            bool presetNameExists(const std::string &name) override;

        private:
            SqliteDatabase &m_db;

            // Helper method to create a ReverbPreset from a statement row
            model::ReverbPreset rowToPreset(SqliteStatement &stmt) const;
        };

    } // namespace database
} // namespace jucyaudio
