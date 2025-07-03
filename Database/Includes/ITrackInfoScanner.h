#pragma once

#include <Database/Includes/TrackInfo.h>

namespace jucyaudio
{
    namespace database
    {
      struct ITrackInfoScanner
      {
          virtual ~ITrackInfoScanner() = default;
          virtual bool processTrack(TrackInfo &trackInfo) = 0;
      };

    } // namespace database
} // namespace jucyaudio
