#pragma once

#include <Database/Includes/IMixMarkerManager.h>
#include <Database/Sqlite/SqliteDatabase.h>

namespace jucyaudio
{
    namespace database
    {
        class SqliteMixMarkerManager : public IMixMarkerManager
        {
        public:
            SqliteMixMarkerManager(SqliteDatabase &db)
                : m_db{db}
            {
            }
            ~SqliteMixMarkerManager() override = default;

            DbResult addMarker(const MixMarker &marker) override;
            std::vector<MixMarker> getMarkersForMix(MixId mixId) const override;
            std::optional<MixMarker> getMarker(MarkerId markerId) const override;
            DbResult updateMarker(const MixMarker &marker) override;
            DbResult deleteMarker(MarkerId markerId) override;
            DbResult deleteMarkersForMix(MixId mixId) override;
            std::optional<MixMarker> findMarkerNearPosition(
                MixId mixId, std::chrono::milliseconds position, std::chrono::milliseconds tolerance) const override;

        private:
            SqliteDatabase &m_db;
        };
    } // namespace database
} // namespace jucyaudio