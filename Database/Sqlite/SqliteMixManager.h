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
            std::vector<MixInfo> getMixes(const TrackQueryArgs &args) const override;
            MixInfo getMix(MixId mixId) const override;
            std::vector<MixTrack> getMixTracks(MixId mixId) const override;
            bool createOrUpdateMix(MixInfo &mixInfo, std::vector<MixTrack> &tracks) const override;
            bool removeMix(MixId mixId) const override;
            bool removeMixes(const std::vector<MixId> &mixIds) const override;
            bool renameMix(MixId mixId, std::string_view name) const override;
            bool createAndSaveAutoMix(const std::vector<TrackInfo> &trackInfos,
                /*in/out*/ MixInfo &mixInfo,
                /*out*/ std::vector<MixTrack> &resultingTracks,
                WorkingSetId source_ws_id,
                const Duration_t defaultCrossfadeDuration = Duration_t{5000}) const override;
            bool removeTrackFromMix(MixId mixId, TrackId trackId) const override;
            bool removeTracksFromMix(MixId mixId, const std::vector<TrackId> &trackIds) const override;
            bool finalizeMix(MixId mixId) const override;
            bool clearMixWorkingSetId(MixId mixId) const override;
            bool updateMixTrack(MixId mixId, const MixTrack& updatedTrack) const override;
            bool setMixStatus(MixId mixId, std::string_view status) const override;

            // Export Organization System methods
            bool setMixExported(MixId mixId, std::string_view exportFolder) const override;
            bool moveBackToMixes(MixId mixId) const override;
            bool isExported(MixId mixId) const override;
            std::vector<ExportFolderInfo> getExportFolders() const override;
            bool createExportFolder(std::string_view name, std::string_view description = "") const override;
            std::vector<MixInfo> getMixesByLocation(std::optional<std::string_view> exportFolder = std::nullopt) const override;

        private:
            database::SqliteDatabase &m_db;
        };

    } // namespace database
} // namespace jucyaudio
