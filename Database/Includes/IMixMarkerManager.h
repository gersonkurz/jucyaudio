#pragma once

#include <Database/Includes/MixMarker.h>
#include <Database/Includes/Constants.h>
#include <optional>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        class IMixMarkerManager
        {
        public:
            virtual ~IMixMarkerManager() = default;

            // Create a new marker for a mix
            virtual DbResult addMarker(const MixMarker &marker) = 0;

            // Get all markers for a specific mix, sorted by position
            virtual std::vector<MixMarker> getMarkersForMix(MixId mixId) const = 0;

            // Get a specific marker by ID
            virtual std::optional<MixMarker> getMarker(MarkerId markerId) const = 0;

            // Update an existing marker
            virtual DbResult updateMarker(const MixMarker &marker) = 0;

            // Delete a specific marker
            virtual DbResult deleteMarker(MarkerId markerId) = 0;

            // Delete all markers for a mix (called when mix is deleted)
            virtual DbResult deleteMarkersForMix(MixId mixId) = 0;

            // Find marker at or near a position (useful for click detection)
            virtual std::optional<MixMarker> findMarkerNearPosition(
                MixId mixId, std::chrono::milliseconds position, std::chrono::milliseconds tolerance) const = 0;
        };
    } // namespace database
} // namespace jucyaudio