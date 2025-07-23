#pragma once

#include <Database/Includes/IMarkerManager.h>
#include <Database/Sqlite/SqliteDatabase.h>
#include <memory>

namespace jucyaudio
{
    namespace database
    {
        class SqliteMarkerManager : public IMarkerManager
        {
        public:
            explicit SqliteMarkerManager(SqliteDatabase& db);
            ~SqliteMarkerManager() override = default;
            
            // IMarkerManager implementation
            MarkerResult createMarker(TrackId trackId, 
                                    std::chrono::milliseconds position, 
                                    const std::string& comment,
                                    MarkerId& outMarkerId) override;
            
            MarkerResult updateMarker(MarkerId markerId, 
                                    const std::string& newComment) override;
            
            MarkerResult deleteMarker(MarkerId markerId) override;
            
            std::vector<TrackMarker> getMarkersForTrack(TrackId trackId) override;
            
            std::optional<TrackMarker> getMarker(MarkerId markerId) override;
            
            MarkerResult deleteAllMarkersForTrack(TrackId trackId) override;
            
        private:
            SqliteDatabase& m_db;
            
            // Helper to convert database row to TrackMarker
            TrackMarker rowToMarker(SqliteStatement& stmt) const;
        };
    }
}