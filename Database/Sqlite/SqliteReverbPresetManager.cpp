#include "SqliteReverbPresetManager.h"
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        SqliteReverbPresetManager::SqliteReverbPresetManager(SqliteDatabase &db)
            : m_db{db}
        {
        }

        SqliteReverbPresetManager::~SqliteReverbPresetManager() = default;

        std::vector<model::ReverbPreset> SqliteReverbPresetManager::getAllPresets()
        {
            std::vector<model::ReverbPreset> presets;

            const char *sql = R"SQL(
            SELECT preset_id, name, settings_json, is_deletable
            FROM ReverbPresets
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

            spdlog::debug("Retrieved {} reverb presets from database", presets.size());
            return presets;
        }

        std::optional<model::ReverbPreset> SqliteReverbPresetManager::getPreset(int64_t presetId)
        {
            const char *sql = R"SQL(
            SELECT preset_id, name, settings_json, is_deletable
            FROM ReverbPresets
            WHERE preset_id = ?
        )SQL";

            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare getPreset query: {}", m_db.getLastError());
                return std::nullopt;
            }

            if (!stmt.addParam(presetId))
            {
                spdlog::error("Failed to bind presetId: {}", m_db.getLastError());
                return std::nullopt;
            }

            if (stmt.getNextResult())
            {
                return rowToPreset(stmt);
            }

            return std::nullopt;
        }

        std::optional<model::ReverbPreset> SqliteReverbPresetManager::savePreset(const std::string &name, const audio::model::ReverbSettings &settings)
        {
            if (name.empty())
            {
                spdlog::error("Cannot save preset with empty name");
                return std::nullopt;
            }

            // Check if name already exists
            if (presetNameExists(name))
            {
                spdlog::warn("Reverb preset with name '{}' already exists", name);
                return std::nullopt;
            }

            const auto settingsJson = settings.toJson().dump();

            const char *sql = R"SQL(
            INSERT INTO ReverbPresets (name, settings_json, is_deletable)
            VALUES (?, ?, 1)
        )SQL";

            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare savePreset query: {}", m_db.getLastError());
                return std::nullopt;
            }

            stmt.addParam(name);
            stmt.addParam(settingsJson);

            if (!stmt.execute())
            {
                spdlog::error("Failed to save reverb preset: {}", m_db.getLastError());
                return std::nullopt;
            }

            const auto presetId = m_db.getLastInsertRowId();
            spdlog::info("Saved reverb preset '{}' with ID {}", name, presetId);

            return model::ReverbPreset{presetId, name, settings, true};
        }

        bool SqliteReverbPresetManager::deletePreset(int64_t presetId)
        {
            // First check if preset exists and is deletable
            const char *checkSql = R"SQL(
            SELECT is_deletable FROM ReverbPresets WHERE preset_id = ?
        )SQL";

            SqliteStatement checkStmt{m_db, checkSql};
            if (!checkStmt.isValid())
            {
                spdlog::error("Failed to prepare check query: {}", m_db.getLastError());
                return false;
            }

            if (!checkStmt.addParam(presetId))
            {
                spdlog::error("Failed to bind presetId: {}", m_db.getLastError());
                return false;
            }

            if (!checkStmt.getNextResult())
            {
                spdlog::warn("Reverb preset {} not found", presetId);
                return false;
            }

            if (checkStmt.getInt64(0) == 0)
            {
                spdlog::warn("Cannot delete factory reverb preset {}", presetId);
                return false;
            }

            // Delete the preset
            const char *sql = R"SQL(
            DELETE FROM ReverbPresets WHERE preset_id = ?
        )SQL";

            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare delete query: {}", m_db.getLastError());
                return false;
            }

            if (!stmt.addParam(presetId))
            {
                spdlog::error("Failed to bind presetId: {}", m_db.getLastError());
                return false;
            }

            if (!stmt.execute())
            {
                spdlog::error("Failed to delete reverb preset {}: {}", presetId, m_db.getLastError());
                return false;
            }

            spdlog::info("Deleted reverb preset {}", presetId);
            return true;
        }

        bool SqliteReverbPresetManager::updatePreset(int64_t presetId, const audio::model::ReverbSettings &settings)
        {
            const auto settingsJson = settings.toJson().dump();

            const char *sql = R"SQL(
            UPDATE ReverbPresets 
            SET settings_json = ?, updated_at = CURRENT_TIMESTAMP
            WHERE preset_id = ?
        )SQL";

            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare update query: {}", m_db.getLastError());
                return false;
            }

            stmt.addParam(settingsJson);
            stmt.addParam(presetId);
            if (!stmt.execute())
            {
                spdlog::error("Failed to update reverb preset {}: {}", presetId, m_db.getLastError());
                return false;
            }

            spdlog::debug("Updated reverb preset {}", presetId);
            return true;
        }

        bool SqliteReverbPresetManager::presetNameExists(const std::string &name)
        {
            const char *sql = R"SQL(
            SELECT COUNT(*) FROM ReverbPresets WHERE name = ?
        )SQL";

            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare name check query: {}", m_db.getLastError());
                return false;
            }

            if (!stmt.addParam(name))
            {
                spdlog::error("Failed to bind name: {}", m_db.getLastError());
                return false;
            }

            if (stmt.getNextResult())
            {
                return stmt.getInt64(0) > 0;
            }

            return false;
        }

        model::ReverbPreset SqliteReverbPresetManager::rowToPreset(SqliteStatement &stmt) const
        {
            model::ReverbPreset preset;

            preset.presetId = stmt.getInt64(0);
            preset.name = stmt.getText(1);

            // Parse JSON settings
            const auto settingsJson = stmt.getText(2);
            try
            {
                const auto json = nlohmann::json::parse(settingsJson);
                preset.settings = audio::model::ReverbSettings::fromJson(json);
            }
            catch (const std::exception &e)
            {
                spdlog::error("Failed to parse reverb settings JSON for preset {}: {}", preset.presetId, e.what());
                preset.settings = audio::model::ReverbSettings{};
            }

            preset.isDeletable = stmt.getInt64(3) != 0;

            return preset;
        }
    } // namespace database
} // namespace jucyaudio