#pragma once

#include <Database/Includes/TrackMarker.h>
#include <Database/Includes/Constants.h>
#include <vector>
#include <optional>

namespace jucyaudio
{
    namespace database
    {
        enum class MarkerResult
        {
            Success,
            ErrorNotFound,
            ErrorDatabase,
            ErrorInvalidInput
        };
        
        class IMarkerManager
        {
        public:
            virtual ~IMarkerManager() = default;
            
            // Create a new marker
            virtual MarkerResult createMarker(TrackId trackId, 
                                            std::chrono::milliseconds position, 
                                            const std::string& comment,
                                            MarkerId& outMarkerId) = 0;
            
            // Update an existing marker
            virtual MarkerResult updateMarker(MarkerId markerId, 
                                            const std::string& newComment) = 0;
            
            // Delete a marker
            virtual MarkerResult deleteMarker(MarkerId markerId) = 0;
            
            // Get all markers for a track
            virtual std::vector<TrackMarker> getMarkersForTrack(TrackId trackId) = 0;
            
            // Get a specific marker
            virtual std::optional<TrackMarker> getMarker(MarkerId markerId) = 0;
            
            // Delete all markers for a track (useful when track is deleted)
            virtual MarkerResult deleteAllMarkersForTrack(TrackId trackId) = 0;
        };
    }
}