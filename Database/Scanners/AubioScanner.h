#pragma once
#include <Database/Includes/ITrackInfoScanner.h>
#include <Database/Includes/TrackInfo.h>
#include <aubio/aubio.h>
#include <filesystem>
#include <string>

namespace jucyaudio
{
    namespace database
    {
        namespace scanners
        {

            class AubioScanner : public ITrackInfoScanner
            {
            private:
                bool processTrack(TrackInfo &trackInfo) override;

                // Default parameters for Aubio, can be configurable later
                const uint_t m_defaultHopSize = 512;
                const uint_t m_defaultWinSize = 1024;
            };

        } // namespace scanners
    } // namespace database
} // namespace jucyaudio