#pragma once

namespace jucyaudio
{
    namespace ui
    {
        enum CustomColourIds
        {
            waveformColourId = 0x2000000,  // Custom ID starting at a high value to avoid conflicts
            folderOnlineTextColourId = 0x2000001,  // Text color for available/online folders
            folderOfflineTextColourId = 0x2000002  // Text color for unavailable/offline folders
        };
    }
}