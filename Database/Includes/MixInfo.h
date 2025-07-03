#pragma once

#include <Database/Includes/Constants.h>
#include <chrono>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        struct MixInfo
        {
            MixId mixId = 0;
            std::string name;
            Timestamp_t timestamp;
            int64_t numberOfTracks{0}; // Number of tracks in the mix, can be used for quick checks
            Duration_t totalDuration{0}; // Total duration of the mix, including crossfades
        };

        struct MixTrack
        {
            MixId mixId = 0;
            TrackId trackId = 0;
            int orderInMix = 0; // 0-based order within the mix

            // Timing within original track
            Duration_t silenceStart{0}; // Skip quiet intro
            Duration_t fadeInStart{0};  // Where fade-in begins
            Duration_t fadeInEnd{0};    // Where track reaches full volume
            Duration_t fadeOutStart{0}; // Where fade-out begins
            Duration_t fadeOutEnd{0};   // Where track goes silent
            Duration_t cutoffTime{0};   // Hard cut (ignore rest of file)

            // Volume levels (for normalization/"leiser" correction)
            Volume_t volumeAtStart = 1 * VOLUME_NORMALIZATION; // After fade-in (linear gain multiplier)
            Volume_t volumeAtEnd = 1 * VOLUME_NORMALIZATION;   // Before fade-out (linear gain multiplier)

            // Mix timeline positioning
            Duration_t mixStartTime{0}; // When this track starts in final mix

            // Crossfade with previous track - ms from end of previous track
            Duration_t crossfadeDuration{5000};
       };



    } // namespace database
} // namespace jucyaudio