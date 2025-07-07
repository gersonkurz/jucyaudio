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
            if (m_mixId != mixId)
            {
                m_mixId = mixId;
                load(); // Reload data for the new mix ID
            }
        }

        // Public method to explicitly reload data if needed
        void MixProjectLoader::load()
        {
            m_mixTracks = theTrackLibrary.getMixManager().getMixTracks(m_mixId);
            m_trackInfos = theTrackLibrary.getTracks(getMixTrackQueryArgs(m_mixId));

            m_trackInfosMap.clear();
            for (const auto &ti : m_trackInfos)
            {
                m_trackInfosMap[ti.trackId] = &ti;
            }
        }

    } // namespace audio
} // namespace jucyaudio
