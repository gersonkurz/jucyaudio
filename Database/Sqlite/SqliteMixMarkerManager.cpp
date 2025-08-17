#include "SqliteMixMarkerManager.h"
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <spdlog/spdlog.h>
#include <chrono>

namespace jucyaudio::database
{
    DbResult SqliteMixMarkerManager::addMarker(const MixMarker& marker)
    {
        const auto now = std::chrono::system_clock::now();
        const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        
        SqliteStatement stmt{m_db, R"SQL(
            INSERT INTO MixMarkers (mix_id, position_ms, comment, color, emoji, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        )SQL"};
        
        if (!stmt.isValid())
        {
            return DbResult::failure(DbResultStatus::ErrorDB, "Failed to prepare add marker statement");
        }
        
        stmt.addParam(marker.mix_id);
        stmt.addParam(marker.position.count());
        stmt.addParam(marker.comment);
        
        if (marker.color.has_value())
            stmt.addParam(marker.color.value());
        else
            stmt.addNullParam();
            
        if (marker.emoji.has_value())
            stmt.addParam(marker.emoji.value());
        else
            stmt.addNullParam();
            
        stmt.addParam(timestamp);
        stmt.addParam(timestamp);
        
        if (!stmt.execute())
        {
            return DbResult::failure(DbResultStatus::ErrorDB, "Failed to add mix marker: " + m_db.getLastError());
        }
        
        return DbResult::success();
    }
    
    std::vector<MixMarker> SqliteMixMarkerManager::getMarkersForMix(MixId mixId) const
    {
        std::vector<MixMarker> markers;
        
        SqliteStatement stmt{m_db, R"SQL(
            SELECT marker_id, mix_id, position_ms, comment, color, emoji, created_at, updated_at
            FROM MixMarkers
            WHERE mix_id = ?
            ORDER BY position_ms
        )SQL"};
        
        if (!stmt.isValid())
        {
            spdlog::error("Failed to prepare get markers statement");
            return markers;
        }
        
        stmt.addParam(mixId);
        
        while (stmt.getNextResult())
        {
            MixMarker marker;
            marker.marker_id = stmt.getInt64(0);
            marker.mix_id = stmt.getInt64(1);
            marker.position = std::chrono::milliseconds{stmt.getInt64(2)};
            marker.comment = stmt.getText(3);
            
            if (!stmt.isNull(4))
                marker.color = stmt.getText(4);
            
            if (!stmt.isNull(5))
                marker.emoji = stmt.getText(5);
            
            const auto created_seconds = stmt.getInt64(6);
            const auto updated_seconds = stmt.getInt64(7);
            
            marker.created_at = std::chrono::system_clock::time_point{std::chrono::seconds{created_seconds}};
            marker.updated_at = std::chrono::system_clock::time_point{std::chrono::seconds{updated_seconds}};
            
            markers.push_back(marker);
        }
        
        return markers;
    }
    
    std::optional<MixMarker> SqliteMixMarkerManager::getMarker(MarkerId markerId) const
    {
        SqliteStatement stmt{m_db, R"SQL(
            SELECT marker_id, mix_id, position_ms, comment, color, emoji, created_at, updated_at
            FROM MixMarkers
            WHERE marker_id = ?
        )SQL"};
        
        if (!stmt.isValid())
        {
            spdlog::error("Failed to prepare get marker statement");
            return std::nullopt;
        }
        
        stmt.addParam(markerId);
        
        if (stmt.getNextResult())
        {
            MixMarker marker;
            marker.marker_id = stmt.getInt64(0);
            marker.mix_id = stmt.getInt64(1);
            marker.position = std::chrono::milliseconds{stmt.getInt64(2)};
            marker.comment = stmt.getText(3);
            
            if (!stmt.isNull(4))
                marker.color = stmt.getText(4);
            
            if (!stmt.isNull(5))
                marker.emoji = stmt.getText(5);
            
            const auto created_seconds = stmt.getInt64(6);
            const auto updated_seconds = stmt.getInt64(7);
            
            marker.created_at = std::chrono::system_clock::time_point{std::chrono::seconds{created_seconds}};
            marker.updated_at = std::chrono::system_clock::time_point{std::chrono::seconds{updated_seconds}};
            
            return marker;
        }
        
        return std::nullopt;
    }
    
    DbResult SqliteMixMarkerManager::updateMarker(const MixMarker& marker)
    {
        const auto now = std::chrono::system_clock::now();
        const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        
        SqliteStatement stmt{m_db, R"SQL(
            UPDATE MixMarkers
            SET position_ms = ?, comment = ?, color = ?, emoji = ?, updated_at = ?
            WHERE marker_id = ?
        )SQL"};
        
        if (!stmt.isValid())
        {
            return DbResult::failure(DbResultStatus::ErrorDB, "Failed to prepare update marker statement");
        }
        
        stmt.addParam(marker.position.count());
        stmt.addParam(marker.comment);
        
        if (marker.color.has_value())
            stmt.addParam(marker.color.value());
        else
            stmt.addNullParam();
            
        if (marker.emoji.has_value())
            stmt.addParam(marker.emoji.value());
        else
            stmt.addNullParam();
            
        stmt.addParam(timestamp);
        stmt.addParam(marker.marker_id);
        
        if (!stmt.execute())
        {
            return DbResult::failure(DbResultStatus::ErrorDB, "Failed to update mix marker: " + m_db.getLastError());
        }
        
        return DbResult::success();
    }
    
    DbResult SqliteMixMarkerManager::deleteMarker(MarkerId markerId)
    {
        SqliteStatement stmt{m_db, "DELETE FROM MixMarkers WHERE marker_id = ?"};
        
        if (!stmt.isValid())
        {
            return DbResult::failure(DbResultStatus::ErrorDB, "Failed to prepare delete marker statement");
        }
        
        stmt.addParam(markerId);
        
        if (!stmt.execute())
        {
            return DbResult::failure(DbResultStatus::ErrorDB, "Failed to delete mix marker: " + m_db.getLastError());
        }
        
        return DbResult::success();
    }
    
    DbResult SqliteMixMarkerManager::deleteMarkersForMix(MixId mixId)
    {
        SqliteStatement stmt{m_db, "DELETE FROM MixMarkers WHERE mix_id = ?"};
        
        if (!stmt.isValid())
        {
            return DbResult::failure(DbResultStatus::ErrorDB, "Failed to prepare delete markers for mix statement");
        }
        
        stmt.addParam(mixId);
        
        if (!stmt.execute())
        {
            return DbResult::failure(DbResultStatus::ErrorDB, "Failed to delete mix markers: " + m_db.getLastError());
        }
        
        return DbResult::success();
    }
    
    std::optional<MixMarker> SqliteMixMarkerManager::findMarkerNearPosition(MixId mixId, std::chrono::milliseconds position, std::chrono::milliseconds tolerance) const
    {
        const auto minPos = (position - tolerance).count();
        const auto maxPos = (position + tolerance).count();
        
        SqliteStatement stmt{m_db, R"SQL(
            SELECT marker_id, mix_id, position_ms, comment, color, emoji, created_at, updated_at
            FROM MixMarkers
            WHERE mix_id = ? AND position_ms >= ? AND position_ms <= ?
            ORDER BY ABS(position_ms - ?)
            LIMIT 1
        )SQL"};
        
        if (!stmt.isValid())
        {
            spdlog::error("Failed to prepare find marker near position statement");
            return std::nullopt;
        }
        
        stmt.addParam(mixId);
        stmt.addParam(minPos);
        stmt.addParam(maxPos);
        stmt.addParam(position.count());
        
        if (stmt.getNextResult())
        {
            MixMarker marker;
            marker.marker_id = stmt.getInt64(0);
            marker.mix_id = stmt.getInt64(1);
            marker.position = std::chrono::milliseconds{stmt.getInt64(2)};
            marker.comment = stmt.getText(3);
            
            if (!stmt.isNull(4))
                marker.color = stmt.getText(4);
            
            if (!stmt.isNull(5))
                marker.emoji = stmt.getText(5);
            
            const auto created_seconds = stmt.getInt64(6);
            const auto updated_seconds = stmt.getInt64(7);
            
            marker.created_at = std::chrono::system_clock::time_point{std::chrono::seconds{created_seconds}};
            marker.updated_at = std::chrono::system_clock::time_point{std::chrono::seconds{updated_seconds}};
            
            return marker;
        }
        
        return std::nullopt;
    }
}