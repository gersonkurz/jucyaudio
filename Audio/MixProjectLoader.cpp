#include <Audio/MixProjectLoader.h>
#include <Utils/StringWriter.h>
#include <Utils/AssortedUtils.h>

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
            int index = 0;
            StringWriter writer;
            for (const auto &mixTrack : m_mixTracks)
            {
                if (const auto it = m_trackInfosMap.find(mixTrack.trackId); it != m_trackInfosMap.end())
                {
                    writer.appendFormatted("Index {}: orderInMix: {}, MixTrack: ID: {}, Track ID: {}: {} - {}\n",
                        index,
                        mixTrack.orderInMix,
                        it->second->trackId,
                        mixTrack.trackId,
                        it->second->artist_name,
                        it->second->title);
                    assert(index == mixTrack.orderInMix);
                }
                else
                {
                    writer.appendFormatted("MixProjectLoader: Track info not found for track ID: {}\n", mixTrack.trackId);
                }
                ++index;
            }
            spdlog::info("MixProjectLoader: Loaded mix project with ID {}:\n{}", m_mixId, writer.asString());
            spdlog::info("MixProjectLoader: Indexed {} track infos for mix ID {}", m_trackInfosMap.size(), m_mixId);
        }


    } // namespace audio
} // namespace jucyaudio
