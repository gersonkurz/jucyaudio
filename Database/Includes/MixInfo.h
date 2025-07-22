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
            WorkingSetId source_ws_id = 0;
            std::string status{"New"};
        };

        struct EnvelopePoint
        {
            Duration_t time;
            Volume_t volume;
        };

        struct MixTrack
        {
            MixId mixId = 0;
            TrackId trackId = 0;
            int orderInMix = 0; // 0-based order within the mix

            // Volume envelope points, encoded / decoded as json
            std::vector<EnvelopePoint> envelopePoints;

            // Mix timeline positioning
            Duration_t mixStartTime{0};
            Duration_t mixEndTime{0};

            bool is_active = true;
       };



    } // namespace database
} // namespace jucyaudio