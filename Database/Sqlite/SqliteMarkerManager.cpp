#include <Database/Sqlite/SqliteMarkerManager.h>
#include <Database/Sqlite/SqliteStatement.h>
#include <Database/Sqlite/SqliteTransaction.h>
#include <spdlog/spdlog.h>
#include <chrono>

namespace jucyaudio
{
    namespace database
    {
        SqliteMarkerManager::SqliteMarkerManager(SqliteDatabase& db)
            : m_db(db)
        {
        }
        
        MarkerResult SqliteMarkerManager::createMarker(TrackId trackId, 
                                                      std::chrono::milliseconds position, 
                                                      const std::string& comment,
                                                      MarkerId& outMarkerId)
        {
            if (!m_db.isValid())
            {
                spdlog::error("Database not open for creating marker");
                return MarkerResult::ErrorDatabase;
            }
            
            if (comment.empty())
            {
                spdlog::error("Cannot create marker with empty comment");
                return MarkerResult::ErrorInvalidInput;
            }
            
            const auto now = std::chrono::system_clock::now();
            const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count();
            
            const char* sql = R"SQL(
                INSERT INTO TrackMarkers (track_id, position_ms, comment, created_at, updated_at)
                VALUES (?, ?, ?, ?, ?)
            )SQL";
            
            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare statement for marker creation");
                return MarkerResult::ErrorDatabase;
            }
            
            stmt.addParam(static_cast<int64_t>(trackId));
            stmt.addParam(static_cast<int64_t>(position.count()));
            stmt.addParam(comment);
            stmt.addParam(timestamp);
            stmt.addParam(timestamp);
            
            if (!stmt.execute())
            {
                spdlog::error("Failed to insert marker: {}", m_db.getLastError());
                return MarkerResult::ErrorDatabase;
            }
            
            outMarkerId = m_db.getLastInsertRowId();
            spdlog::info("Created marker {} for track {} at {}ms", outMarkerId, trackId, position.count());
            
            return MarkerResult::Success;
        }
        
        MarkerResult SqliteMarkerManager::updateMarker(MarkerId markerId, 
                                                      const std::string& newComment)
        {
            if (!m_db.isValid())
            {
                spdlog::error("Database not open for updating marker");
                return MarkerResult::ErrorDatabase;
            }
            
            if (newComment.empty())
            {
                spdlog::error("Cannot update marker with empty comment");
                return MarkerResult::ErrorInvalidInput;
            }
            
            const auto now = std::chrono::system_clock::now();
            const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count();
            
            const char* sql = R"SQL(
                UPDATE TrackMarkers 
                SET comment = ?, updated_at = ?
                WHERE marker_id = ?
            )SQL";
            
            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare statement for marker update");
                return MarkerResult::ErrorDatabase;
            }
            
            stmt.addParam(newComment);
            stmt.addParam(timestamp);
            stmt.addParam(static_cast<int64_t>(markerId));
            
            if (!stmt.execute())
            {
                spdlog::error("Failed to update marker: {}", m_db.getLastError());
                return MarkerResult::ErrorDatabase;
            }
            
            if (m_db.getChangesCount() == 0)
            {
                spdlog::error("No marker found with ID {}", markerId);
                return MarkerResult::ErrorNotFound;
            }
            
            spdlog::info("Updated marker {}", markerId);
            return MarkerResult::Success;
        }
        
        MarkerResult SqliteMarkerManager::deleteMarker(MarkerId markerId)
        {
            if (!m_db.isValid())
            {
                spdlog::error("Database not open for deleting marker");
                return MarkerResult::ErrorDatabase;
            }
            
            const char* sql = "DELETE FROM TrackMarkers WHERE marker_id = ?";
            
            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare statement for marker deletion");
                return MarkerResult::ErrorDatabase;
            }
            
            stmt.addParam(static_cast<int64_t>(markerId));
            
            if (!stmt.execute())
            {
                spdlog::error("Failed to delete marker: {}", m_db.getLastError());
                return MarkerResult::ErrorDatabase;
            }
            
            if (m_db.getChangesCount() == 0)
            {
                spdlog::error("No marker found with ID {}", markerId);
                return MarkerResult::ErrorNotFound;
            }
            
            spdlog::info("Deleted marker {}", markerId);
            return MarkerResult::Success;
        }
        
        std::vector<TrackMarker> SqliteMarkerManager::getMarkersForTrack(TrackId trackId)
        {
            std::vector<TrackMarker> markers;
            
            if (!m_db.isValid())
            {
                spdlog::error("Database not open for getting markers");
                return markers;
            }
            
            const char* sql = R"SQL(
                SELECT marker_id, track_id, position_ms, comment, created_at, updated_at, color, emoji
                FROM TrackMarkers
                WHERE track_id = ?
                ORDER BY position_ms ASC
            )SQL";
            
            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare statement for getting markers");
                return markers;
            }
            
            stmt.addParam(static_cast<int64_t>(trackId));
            
            while (stmt.getNextResult())
            {
                markers.push_back(rowToMarker(stmt));
            }
            
            spdlog::debug("Found {} markers for track {}", markers.size(), trackId);
            return markers;
        }
        
        std::optional<TrackMarker> SqliteMarkerManager::getMarker(MarkerId markerId)
        {
            if (!m_db.isValid())
            {
                spdlog::error("Database not open for getting marker");
                return std::nullopt;
            }
            
            const char* sql = R"SQL(
                SELECT marker_id, track_id, position_ms, comment, created_at, updated_at, color, emoji
                FROM TrackMarkers
                WHERE marker_id = ?
            )SQL";
            
            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare statement for getting marker");
                return std::nullopt;
            }
            
            stmt.addParam(static_cast<int64_t>(markerId));
            
            if (!stmt.execute())
            {
                return rowToMarker(stmt);
            }
            
            return std::nullopt;
        }
        
        MarkerResult SqliteMarkerManager::deleteAllMarkersForTrack(TrackId trackId)
        {
            if (!m_db.isValid())
            {
                spdlog::error("Database not open for deleting markers");
                return MarkerResult::ErrorDatabase;
            }
            
            const char* sql = "DELETE FROM TrackMarkers WHERE track_id = ?";
            
            SqliteStatement stmt{m_db, sql};
            if (!stmt.isValid())
            {
                spdlog::error("Failed to prepare statement for deleting track markers");
                return MarkerResult::ErrorDatabase;
            }
            
            stmt.addParam(static_cast<int64_t>(trackId));
            
            if (!stmt.execute())
            {
                spdlog::error("Failed to delete markers: {}", m_db.getLastError());
                return MarkerResult::ErrorDatabase;
            }
            
            const auto deletedCount = m_db.getChangesCount();
            spdlog::info("Deleted {} markers for track {}", deletedCount, trackId);
            
            return MarkerResult::Success;
        }
        
        TrackMarker SqliteMarkerManager::rowToMarker(SqliteStatement& stmt) const
        {
            TrackMarker marker;
            
            marker.markerId = stmt.getInt64(0);
            marker.trackId = static_cast<TrackId>(stmt.getInt64(1));
            marker.position = std::chrono::milliseconds(stmt.getInt64(2));
            marker.comment = stmt.getText(3);
            
            // Convert timestamps
            auto createdTimestamp = stmt.getInt64(4);
            auto updatedTimestamp = stmt.getInt64(5);
            
            marker.createdAt = std::chrono::system_clock::time_point(
                std::chrono::seconds(createdTimestamp));
            marker.updatedAt = std::chrono::system_clock::time_point(
                std::chrono::seconds(updatedTimestamp));
            
            // Optional fields
            if (!stmt.isNull(6))
            {
                marker.color = stmt.getText(6);
            }
            if (!stmt.isNull(7))
            {
                marker.emoji = stmt.getText(7);
            }
            
            return marker;
        }
    }
}