#pragma once

#include <Database/Includes/Constants.h>
#include <chrono>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

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
            // Database fields
            MixId mixId = 0;
            TrackId trackId = 0;
            int orderInMix = 0; // 0-based order within the mix

            // --- CUE POINTS ---
            // Define the audible portion of the track. Both are relative offsets.

            /**
             * @brief Offset from the START of the track (time 0).
             * A negative value means the audible portion starts before the audio, adding silence at the beginning.
             * A positive value means the audible portion starts after the audio begins, truncating the start.
             * Example: -5s adds 5s of silence; +5s skips the first 5s of audio.
             */
            Duration_t cueStart{0};

            /**
             * @brief Offset from the END of the track (trackDuration).
             * A negative value means the audible portion ends before the audio finishes, truncating the end.
             * A positive value means the audible portion ends after the audio finishes, adding silence at the end.
             * Example: -5s skips the last 5s of audio; +5s adds 5s of silence.
             */
            Duration_t cueEnd{0};
            
            // Attach points - define how tracks connect
            Duration_t attachFrom{0};   // Where this track starts overlapping with previous
            Duration_t attachTo{0};     // Where next track starts overlapping with this one
            
            // Volume envelope - 6 points for crossfade curve
            std::vector<EnvelopePoint> envelopePoints;
            
            // Computed properties (not stored in DB)
            /**
             * @brief Calculates the actual end time of the audible portion as an absolute time from the track's start.
             * @param trackDuration The natural duration of the source audio file.
             * @return The absolute time position where the audible part of this track ends.
             */
            Duration_t getCueEndActual(Duration_t trackDuration) const
            {
                // The actual end is the track's natural duration plus the cueEnd offset.
                return trackDuration + cueEnd;
            }

            /**
             * @brief Calculates the total duration of the component on the timeline.
             * @param trackDuration The natural duration of the source audio file.
             * @return The total duration from the (potentially negative) cueStart to the (potentially extended) cueEnd.
             */
            Duration_t getEffectiveDuration(Duration_t trackDuration) const
            {
                // The total duration is simply the difference between the absolute end time and the start time.
                return getCueEndActual(trackDuration) - cueStart;
            }
       };



    } // namespace database
} // namespace jucyaudio

// JSON serialization for the new structures
namespace nlohmann {
    template <>
    struct adl_serializer<jucyaudio::database::EnvelopePoint> {
        static void to_json(json& j, const jucyaudio::database::EnvelopePoint& p) {
            j = json{{"time", p.time.count()}, {"volume", p.volume}};
        }
        
        static void from_json(const json& j, jucyaudio::database::EnvelopePoint& p) {
            p.time = jucyaudio::Duration_t(j.at("time").get<int64_t>());
            p.volume = j.at("volume").get<jucyaudio::Volume_t>();
        }
    };
    
    template <>
    struct adl_serializer<jucyaudio::database::MixTrack> {
        static void to_json(json& j, const jucyaudio::database::MixTrack& mt) {
            // Only serialize the mix_data portion (not mixId, trackId, orderInMix)
            j = json{
                {"cue", {
                    {"start", mt.cueStart.count()},
                    {"end", mt.cueEnd.count()}
                }},
                {"attach", {
                    {"from", mt.attachFrom.count()},
                    {"to", mt.attachTo.count()}
                }},
                {"envelope", mt.envelopePoints}
            };
        }
        
        static void from_json(const json& j, jucyaudio::database::MixTrack& mt) {
            auto cue = j.at("cue");
            mt.cueStart = jucyaudio::Duration_t(cue.at("start").get<int64_t>());
            mt.cueEnd = jucyaudio::Duration_t(cue.at("end").get<int64_t>());
            
            auto attach = j.at("attach");
            mt.attachFrom = jucyaudio::Duration_t(attach.at("from").get<int64_t>());
            mt.attachTo = jucyaudio::Duration_t(attach.at("to").get<int64_t>());
            
            mt.envelopePoints = j.at("envelope").get<std::vector<jucyaudio::database::EnvelopePoint>>();
        }
    };
}