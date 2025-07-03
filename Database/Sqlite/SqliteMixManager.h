#pragma once

#include <Database/Includes/IMixManager.h>
#include <Database/Includes/ITrackDatabase.h>
#include <Database/Sqlite/SqliteDatabase.h>

namespace jucyaudio
{
    namespace database
    {
        class SqliteMixManager : public IMixManager
        {
        public:
            SqliteMixManager(database::SqliteDatabase &db)
                : m_db{db}
            {
            }
            ~SqliteMixManager() override = default;

        private:
            std::vector<MixInfo> getMixes(const TrackQueryArgs& args) const override;
            std::vector<MixTrack> getMixTracks(MixId mixId) const override;
            bool createOrUpdateMix(MixInfo &mixInfo, std::vector<MixTrack> &tracks) const override;
            bool removeMix(MixId mixId) const override;
            bool createAndSaveAutoMix(const std::vector<TrackInfo> &trackInfos,
                                      /*in/out*/ MixInfo &mixInfo,
                                      /*out*/ std::vector<MixTrack> &resultingTracks, const Duration_t defaultCrossfadeDuration // Default 5s
                                      ) const override;

        private:
            database::SqliteDatabase &m_db;
        };

    } // namespace database
} // namespace jucyaudio
