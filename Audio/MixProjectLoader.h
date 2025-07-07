#pragma once

#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/TrackLibrary.h>
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
            void loadMix(MixId mixId);

            // --- Public Accessors ---
            MixId getMixId() const
            {
                return m_mixId;
            }
            const std::vector<MixTrack> &getMixTracks() const
            {
                return m_mixTracks;
            }
            const TrackInfo *getTrackInfoForId(TrackId trackId) const
            {
                const auto it = m_trackInfosMap.find(trackId);
                return (it != m_trackInfosMap.end()) ? it->second : nullptr;
            }

        private:

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
            std::vector<MixTrack> m_mixTracks;
            std::vector<TrackInfo> m_trackInfos;
            std::unordered_map<TrackId, const TrackInfo *> m_trackInfosMap;
        };
    } // namespace audio
} // namespace jucyaudio
