#pragma once

namespace jucyaudio
{
    namespace ui
    {
        enum CustomColourIds
        {
            waveformColourId = 0x2000000,         // Custom ID starting at a high value to avoid conflicts
            folderOnlineTextColourId = 0x2000001, // Text color for available/online folders
            folderOfflineTextColourId = 0x2000002, // Text color for unavailable/offline folders
            accentColourId = 0x2000003,             // Accent color for icons and other UI elements
            mainBackgroundColourId = 0x2000004,    // Main background color for the application
            alternateBackgroundColourId = 0x2000005, // Alternate background color for contrast
            mainForegroundColourId = 0x2000006,      // Main foreground color for text and icons
            disabledForegroundColourId = 0x2000007, // Alternate foreground color for text and icons
        };
    } // namespace ui
} // namespace jucyaudio