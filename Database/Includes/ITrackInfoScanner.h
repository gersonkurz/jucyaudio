#pragma once

#include <Database/Includes/TrackInfo.h>

namespace jucyaudio
{
    namespace database
    {
      struct ITrackInfoScanner
      {
          virtual ~ITrackInfoScanner() = default;
          /**
           * @brief Fill in what can be read from this file.
           *
           * @return The fields actually established. None when nothing could be read. Anything the
           *         return value does not name is left at its default in @p trackInfo, and a caller
           *         writing that default over an existing row would be erasing what the library knew
           *         rather than recording what the file says.
           */
          virtual ScannedFields processTrack(TrackInfo &trackInfo, const std::filesystem::path& path) = 0;
      };

    } // namespace database
} // namespace jucyaudio
