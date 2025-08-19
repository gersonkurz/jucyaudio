#include "SqliteEQPresetManager.h"
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        SqliteEQPresetManager::SqliteEQPresetManager(SqliteDatabase &db)
            : m_db{db}
        {
        }

        std::vector<model::EQPreset> SqliteEQPresetManager::getAllPresets()
        {
            std::vector<model::EQPreset> presets;

            const char *sql = R"SQL(
            SELECT preset_id, name, is_deletable, settings_json
            FROM EQPresets
            ORDER BY is_deletable ASC, name ASC
        )SQL";

            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare getAllPresets query: {}", m_db.getLastError());
                return presets;
            }

            while (stmt.getNextResult())
            {
                presets.push_back(rowToPreset(stmt));
            }

            return presets;
        }

        std::optional<model::EQPreset> SqliteEQPresetManager::savePreset(const juce::String &name, const audio::model::EQSettings &settings)
        {
            if (name.isEmpty())
            {
                spdlog::error("Cannot save preset with empty name");
                return std::nullopt;
            }

            // Check if a preset with this name already exists
            if (presetNameExists(name))
            {
                // Update existing preset
                const char *updateSql = R"SQL(
                UPDATE EQPresets 
                SET settings_json = ?
                WHERE name = ?
            )SQL";

                SqliteStatement updateStmt{m_db, updateSql};
                if (!updateStmt.isValid())
                {
                    spdlog::error("Failed to prepare update preset query: {}", m_db.getLastError());
                    return std::nullopt;
                }

                updateStmt.addParam(settings.toJson().toStdString());
                updateStmt.addParam(name.toStdString());

                if (!updateStmt.execute())
                {
                    spdlog::error("Failed to update preset: {}", m_db.getLastError());
                    return std::nullopt;
                }

                // Get the updated preset
                const char *selectSql = "SELECT preset_id FROM EQPresets WHERE name = ?";
                SqliteStatement selectStmt{m_db, selectSql};
                selectStmt.addParam(name.toStdString());

                if (selectStmt.getNextResult())
                {
                    return getPreset(selectStmt.getInt64(0));
                }
            }
            else
            {
                // Insert new preset
                const char *insertSql = R"SQL(
                INSERT INTO EQPresets (name, is_deletable, settings_json)
                VALUES (?, 1, ?)
            )SQL";

                SqliteStatement insertStmt{m_db, insertSql};
                if (!insertStmt.isValid())
                {
                    spdlog::error("Failed to prepare insert preset query: {}", m_db.getLastError());
                    return std::nullopt;
                }

                insertStmt.addParam(name.toStdString());
                insertStmt.addParam(settings.toJson().toStdString());

                if (!insertStmt.execute())
                {
                    spdlog::error("Failed to insert preset: {}", m_db.getLastError());
                    return std::nullopt;
                }

                // Get the newly created preset
                const auto presetId = m_db.getLastInsertRowId();
                return getPreset(presetId);
            }

            return std::nullopt;
        }

        bool SqliteEQPresetManager::deletePreset(int64_t presetId)
        {
            // First check if the preset is deletable
            const char *checkSql = "SELECT is_deletable FROM EQPresets WHERE preset_id = ?";
            SqliteStatement checkStmt{m_db, checkSql};
            checkStmt.addParam(presetId);

            if (!checkStmt.getNextResult())
            {
                spdlog::error("Preset {} not found", presetId);
                return false;
            }

            if (checkStmt.getInt64(0) == 0)
            {
                spdlog::warn("Cannot delete system preset {}", presetId);
                return false;
            }

            // Delete the preset
            const char *deleteSql = "DELETE FROM EQPresets WHERE preset_id = ?";
            SqliteStatement deleteStmt{m_db, deleteSql};
            deleteStmt.addParam(presetId);

            if (!deleteStmt.execute())
            {
                spdlog::error("Failed to delete preset {}: {}", presetId, m_db.getLastError());
                return false;
            }

            return true;
        }

        std::optional<model::EQPreset> SqliteEQPresetManager::getPreset(int64_t presetId)
        {
            const char *sql = R"SQL(
            SELECT preset_id, name, is_deletable, settings_json
            FROM EQPresets
            WHERE preset_id = ?
        )SQL";

            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare getPreset query: {}", m_db.getLastError());
                return std::nullopt;
            }

            stmt.addParam(presetId);

            if (stmt.getNextResult())
            {
                return rowToPreset(stmt);
            }

            return std::nullopt;
        }

        bool SqliteEQPresetManager::presetNameExists(const juce::String &name)
        {
            const char *sql = "SELECT COUNT(*) FROM EQPresets WHERE name = ?";
            SqliteStatement stmt{m_db, sql};

            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare presetNameExists query: {}", m_db.getLastError());
                return false;
            }

            stmt.addParam(name.toStdString());

            if (stmt.getNextResult())
            {
                return stmt.getInt64(0) > 0;
            }

            return false;
        }

        model::EQPreset SqliteEQPresetManager::rowToPreset(database::SqliteStatement &stmt) const
        {
            model::EQPreset preset;
            preset.presetId = stmt.getInt64(0);
            preset.name = stmt.getText(1);
            preset.isDeletable = stmt.getInt64(2) != 0;

            // Parse JSON settings
            const auto jsonString = juce::String(stmt.getText(3));
            preset.settings = audio::model::EQSettings::fromJson(jsonString);

            return preset;
        }
    } // namespace database
} // namespace jucyaudio