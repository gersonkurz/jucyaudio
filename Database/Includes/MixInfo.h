#pragma once

#include <Database/Includes/Constants.h>
#include <chrono>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

/*
This is our common conceptual model of a mix:

1. The Track Segment (The What)

- Each track in a mix is a conceptual block defined by cueStart and cueEnd 
  relative to its source waveform.
- This block consists of three parts: `[silence-before] - [waveform-content] - [silence-after]`.
- The Envelope Points control the volume over this entire block. Their time values are
  relative to the start of the source waveform, allowing them to exist in the silence regions.

2. Attach Points (The Where)

- Each track has two sync markers, attachFrom and attachTo, whose time values are relative
  to the start of the source waveform.
- Constraint: These markers must be located within the track segment's effective duration. 
  That is, `cueStart <= attach_point_time <= getCueEndActual(trackDuration)`. They are independent 
  of the audio content and define where one track segment links to the next.

3. Mix Flow (The Algorithm)

- The final timeline is built iteratively.
- Anchor: Track 1's audio begins at an absolute time of `max(0, -Track1.cueStart)`. 
  This establishes the AudioStartTime for the first track and may create initial silence for 
  the whole mix. 
- Placement Rule: For every subsequent track N, its audio content is placed on the timeline
  according to the confirmed formula: 

  `AudioStartTime(N) = AudioStartTime(N-1) + Track(N-1).attachTo - Track(N).attachFrom`

- Result: A deterministic sequence of precisely positioned audio blocks whose content and
  volume are defined by their individual cue and envelope points.

*/
namespace jucyaudio
{
    namespace database
    {
        struct MixInfo
        {
            MixId mixId = 0;
            std::string name;
            Timestamp_t timestamp;
            int64_t numberOfTracks{0};   // Number of tracks in the mix, can be used for quick checks
            Duration_t totalDuration{0}; // Total duration of the mix, including crossfades
            WorkingSetId source_ws_id = 0;
            std::string status{"New"};
        };

        /**
         * @brief Represents a single point in a volume envelope.
         */
        struct EnvelopePoint
        {
            /**
             * @brief The time position of this envelope point.
             * This value is an offset from the START of the source audio file (time 0).
             * It can be negative to control volume in the "silence-before" region created by a negative cueStart.
             */
            Duration_t time;
            Volume_t volume;
        };

        /**
         * @brief Defines a single track's role and position within a mix.
         * This struct embodies the "What vs. Where" architectural principle:
         * - Cue and Envelope points define WHAT the track segment contains (audio, silence, volume).
         * - Attach points define WHERE the track segment is positioned on the timeline.
         */
        struct MixTrack
        {
            // --- DATABASE / IDENTITY FIELDS ---
            MixId mixId = 0;
            TrackId trackId = 0;
            int orderInMix = 0; // 0-based order within the mix

            // --- CUE POINTS (The "What") ---
            // These define the content of the track segment by creating a view into the source audio.

            /**
             * @brief Offset from the START of the source audio file (time 0).
             * This determines the "in-point" of the audible portion.
             * A negative value adds silence before the audio begins.
             * A positive value truncates (skips) the start of the audio.
             * Frame of Reference: Start of source audio.
             */
            Duration_t cueStart{0};

            /**
             * @brief Offset from the END of the source audio file.
             * This determines the "out-point" of the audible portion.
             * A negative value truncates (skips) the end of the audio.
             * A positive value adds silence after the audio finishes.
             * Frame of Reference: End of source audio.
             */
            Duration_t cueEnd{0};

            // --- ATTACH POINTS (The "Where") ---
            // These are synchronization markers used to position this track relative to its neighbors.

            /**
             * @brief The "input" sync point for this track.
             * This time offset from this track's audio start is aligned with the 'attachTo'
             * point of the PREVIOUS track in the mix.
             * Frame of Reference: Start of source audio.
             * Constraint: Must be within the track's effective duration.
             */
            Duration_t attachFrom{0};

            /**
             * @brief The "output" sync point for this track.
             * This time offset from this track's audio start serves as the alignment point
             * for the NEXT track in the mix.
             * Frame of Reference: Start of source audio.
             * Constraint: Must be within the track's effective duration.
             */
            Duration_t attachTo{0};

            /**
             * @brief A series of points defining the volume curve over the track segment's effective duration.
             */
            std::vector<EnvelopePoint> envelopePoints;

            // --- COMPUTED PROPERTIES (Not stored in DB) ---
            /**
             * @brief Calculates the absolute end time of the audible portion, relative to the start of the source audio.
             * @param trackDuration The natural duration of the source audio file.
             * @return The absolute time position where the audible part of this track ends.
             */
            Duration_t getCueEndActual(Duration_t trackDuration) const
            {
                // The actual end is the track's natural duration plus the cueEnd offset.
                return trackDuration + cueEnd;
            }

            /**
             * @brief Calculates the total duration of the component as it appears on the timeline.
             * This includes any added silence from cue points.
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
namespace nlohmann
{
    template <> struct adl_serializer<jucyaudio::database::EnvelopePoint>
    {
        static void to_json(json &j, const jucyaudio::database::EnvelopePoint &p)
        {
            j = json{{"time", p.time.count()}, {"volume", p.volume}};
        }

        static void from_json(const json &j, jucyaudio::database::EnvelopePoint &p)
        {
            p.time = jucyaudio::Duration_t(j.at("time").get<int64_t>());
            p.volume = j.at("volume").get<jucyaudio::Volume_t>();
        }
    };

    template <> struct adl_serializer<jucyaudio::database::MixTrack>
    {
        static void to_json(json &j, const jucyaudio::database::MixTrack &mt)
        {
            // Only serialize the mix_data portion (not mixId, trackId, orderInMix)
            j = json{{"cue", {{"start", mt.cueStart.count()}, {"end", mt.cueEnd.count()}}},
                {"attach", {{"from", mt.attachFrom.count()}, {"to", mt.attachTo.count()}}},
                {"envelope", mt.envelopePoints}};
        }

        static void from_json(const json &j, jucyaudio::database::MixTrack &mt)
        {
            auto cue = j.at("cue");
            mt.cueStart = jucyaudio::Duration_t(cue.at("start").get<int64_t>());
            mt.cueEnd = jucyaudio::Duration_t(cue.at("end").get<int64_t>());

            auto attach = j.at("attach");
            mt.attachFrom = jucyaudio::Duration_t(attach.at("from").get<int64_t>());
            mt.attachTo = jucyaudio::Duration_t(attach.at("to").get<int64_t>());

            mt.envelopePoints = j.at("envelope").get<std::vector<jucyaudio::database::EnvelopePoint>>();
        }
    };
} // namespace nlohmann