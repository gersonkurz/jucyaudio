#pragma once

#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/TrackLibrary.h>
#include <Audio/Model/EQSettings.h>
#include <unordered_map>

namespace jucyaudio
{
    namespace audio
    {
        using namespace database;

        // This class is a simple data holder and loader for a mix project.
        // It is not a component and has no UI.
        class MixProjectLoader
        {
        public:
            // Constructor loads all necessary data from the database.
            MixProjectLoader();
            virtual ~MixProjectLoader() = default;

            MixProjectLoader(const MixProjectLoader &) = delete;            // No copy
            MixProjectLoader &operator=(const MixProjectLoader &) = delete; // No assignment
            MixProjectLoader(MixProjectLoader &&) = delete;                 // No move
            MixProjectLoader &operator=(MixProjectLoader &&) = delete;      // No move assignment

            // Public method to explicitly reload data if needed
            bool loadMix(MixId mixId);
            bool reloadFromDatabase();

            // --- Public Accessors ---
            auto getMixId() const
            {
                return m_mixId;
            }

            auto &getMixTracks()
            {
                return m_mixTracks;
            }
            
            auto& getMixInfo()
            {
                return m_mixInfo;
            }

            // Get/set master EQ settings for this mix
            const model::EQSettings& getMasterEQSettings() const { return m_masterEQSettings; }
            void setMasterEQSettings(const model::EQSettings& settings) { m_masterEQSettings = settings; }

            const TrackInfo *getTrackInfoForId(TrackId trackId) const
            {
                const auto it = m_trackInfosMap.find(trackId);
                return (it != m_trackInfosMap.end()) ? it->second : nullptr;
            }

            const TrackInfo *getTrackInfoForRow(RowIndex_t rowIndex) const
            {
                if (rowIndex >= static_cast<RowIndex_t>(m_mixTracks.size()))
                {
                    return nullptr; // Out of bounds
                }
                return getTrackInfoForId(m_mixTracks[rowIndex].trackId);
            }
            
            Duration_t calculateMixDuration() const;

            // Remove a concrete row from the in-memory mix state without reloading the full mix.
            bool removeTrackAtOrder(int orderInMix);

            // Reorder tracks in the mix
            // @param trackMoves Vector of pairs where first is the track ID and second is the new position
            // @return true if reordering was successful
            bool reorderTracks(const std::vector<std::pair<TrackId, int>>& trackMoves);
            
            // Save the current mix state back to the database
            // @param mixManager The mix manager to use for saving
            // @return true if save was successful
            bool saveMix(const IMixManager& mixManager);

        private:
            void dumpContext(const char *file, int line) const;
            void rebuildTrackInfoMap();
            // Helper to move a single track to a new position
            bool reorderSingleTrack(TrackId trackId, int newPosition);

            // Helper to construct the query args needed to fetch all tracks for this mix.
            TrackQueryArgs getMixTrackQueryArgs(MixId mixId) const
            {
                TrackQueryArgs args;
                args.mixId = mixId;
                args.usePaging = false; // We want all tracks in the mix
                return args;
            }

        protected:
            MixId m_mixId;
            MixInfo m_mixInfo;
            std::vector<MixTrack> m_mixTracks;
            std::vector<TrackInfo> m_trackInfos;
            std::unordered_map<TrackId, const TrackInfo *> m_trackInfosMap;
            model::EQSettings m_masterEQSettings; // Master EQ settings for this mix
        };
    } // namespace audio
} // namespace jucyaudio
