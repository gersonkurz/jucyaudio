#include <Audio/MixProjectLoader.h>

namespace jucyaudio
{
    namespace audio
    {
        // Constructor loads all necessary data from the database.
        MixProjectLoader::MixProjectLoader(MixId mixId, const database::TrackLibrary &trackLibrary)
            : m_mixId{mixId},
              m_trackLibrary{trackLibrary}
        {
            load();
        }

        // Public method to explicitly reload data if needed
        void MixProjectLoader::load()
        {
            m_mixTracks = m_trackLibrary.getMixManager().getMixTracks(m_mixId);
            m_trackInfos = m_trackLibrary.getTracks(getMixTrackQueryArgs(m_mixId));

            m_trackInfosMap.clear();
            for (const auto &ti : m_trackInfos)
            {
                m_trackInfosMap[ti.trackId] = &ti;
            }
        }

    } // namespace audio
} // namespace jucyaudio
