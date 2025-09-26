#pragma once

#include <Database/Includes/IMixManager.h>
#include <Database/Includes/IUndoManager.h>

namespace jucyaudio
{
    namespace database
    {
        /**
         * @brief Decorator for IMixManager that adds undo/redo support.
         * 
         * This class wraps an existing IMixManager implementation and records
         * all modifications to the undo history before delegating to the
         * underlying implementation.
         */
        class SqliteMixManagerWithUndo : public IMixManager
        {
        public:
            SqliteMixManagerWithUndo(IMixManager& wrappedManager, IUndoManager& undoManager)
                : m_wrappedManager{wrappedManager}
                , m_undoManager{undoManager}
            {
            }

            ~SqliteMixManagerWithUndo() override = default;

            // Non-modifying operations - just delegate
            std::vector<MixInfo> getMixes(const TrackQueryArgs& args) const override
            {
                return m_wrappedManager.getMixes(args);
            }

            MixInfo getMix(MixId mixId) const override
            {
                return m_wrappedManager.getMix(mixId);
            }

            std::vector<MixTrack> getMixTracks(MixId mixId) const override
            {
                return m_wrappedManager.getMixTracks(mixId);
            }

            // Modifying operations - record undo then delegate
            bool createOrUpdateMix(MixInfo& mixInfo, std::vector<MixTrack>& tracks) const override;
            bool removeMix(MixId mixId) const override;
            bool removeMixes(const std::vector<MixId>& mixIds) const override;
            bool renameMix(MixId mixId, std::string_view name) const override;
            bool createAndSaveAutoMix(const std::vector<TrackInfo>& trackInfos,
                                    MixInfo& mixInfo,
                                    std::vector<MixTrack>& resultingTracks,
                                    WorkingSetId source_ws_id,
                                    const Duration_t defaultCrossfadeDuration = Duration_t{5000}) const override;
            bool removeTrackFromMix(MixId mixId, TrackId trackId) const override;
            bool removeTracksFromMix(MixId mixId, const std::vector<TrackId>& trackIds) const override;
            bool finalizeMix(MixId mixId) const override;
            bool clearMixWorkingSetId(MixId mixId) const override;
            bool updateMixTrack(MixId mixId, const MixTrack& updatedTrack) const override;
            bool setMixStatus(MixId mixId, std::string_view status) const override;

        private:
            IMixManager& m_wrappedManager;
            IUndoManager& m_undoManager;
        };

    } // namespace database
} // namespace jucyaudio