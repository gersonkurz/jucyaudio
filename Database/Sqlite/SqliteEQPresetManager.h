#pragma once

#include <Database/Includes/IEQPresetManager.h>
#include <Database/Sqlite/SqliteDatabase.h>

namespace jucyaudio::database
{
    class SqliteStatement;
}

namespace jucyaudio::database::sqlite
{
    class SqliteEQPresetManager : public IEQPresetManager
    {
    public:
        explicit SqliteEQPresetManager(SqliteDatabase& db);
        ~SqliteEQPresetManager() override = default;
        
        std::vector<model::EQPreset> getAllPresets() override;
        std::optional<model::EQPreset> savePreset(const juce::String& name, 
                                                  const audio::model::EQSettings& settings) override;
        bool deletePreset(int64_t presetId) override;
        std::optional<model::EQPreset> getPreset(int64_t presetId) override;
        bool presetNameExists(const juce::String& name) override;
        
    private:
        SqliteDatabase& m_db;
        
        // Helper to convert database row to EQPreset
        model::EQPreset rowToPreset(database::SqliteStatement& stmt) const;
    };
    
} // namespace jucyaudio::database::sqlite