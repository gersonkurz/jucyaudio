#include <Audio/MixProjectLoader.h>

namespace jucyaudio
{
    namespace audio
    {
        // Constructor loads all necessary data from the database.
        MixProjectLoader::MixProjectLoader()
            : m_mixId{0}
        {
        }

        void MixProjectLoader::loadMix(MixId mixId)
        {
            spdlog::debug("MixProjectLoader: Loading mix with ID {}", mixId);
            m_mixId = mixId;
            m_mixTracks = theTrackLibrary.getMixManager().getMixTracks(m_mixId);
            spdlog::info("MixProjectLoader: Loaded {} tracks for mix ID {}", m_mixTracks.size(), m_mixId);
            m_trackInfos = theTrackLibrary.getTracks(getMixTrackQueryArgs(m_mixId));
            spdlog::info("MixProjectLoader: Loaded {} track infos for mix ID {}", m_trackInfos.size(), m_mixId);

            m_trackInfosMap.clear();
            for (const auto &ti : m_trackInfos)
            {
                m_trackInfosMap[ti.trackId] = &ti;
            }
            spdlog::info("MixProjectLoader: Indexed {} track infos for mix ID {}", m_trackInfosMap.size(), m_mixId);
        }


    } // namespace audio
} // namespace jucyaudio
