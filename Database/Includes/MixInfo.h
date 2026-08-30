#pragma once

#include <spdlog/spdlog.h>
#include <Database/Includes/Constants.h>
#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
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

            // Export Organization fields
            std::optional<Timestamp_t> exportedAt;  // When the mix was exported
            std::optional<std::string> exportFolder; // Which export folder it's in (NULL = editable)
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

            std::string toString() const
            {
                return std::format("EnvelopePoint(time: {}, volume: {})", time.count(), volume);
            }

            bool operator==(const EnvelopePoint& other) const
            {
                return time == other.time && volume == other.volume;
            }

            bool operator!=(const EnvelopePoint& other) const
            {
                return !(*this == other);
            }
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

            /**
             * @brief Per-track gain adjustment. Applied as a linear multiplier to the track's audio.
             * Default is 1.0f (no change). Values < 1.0f reduce volume, > 1.0f increase volume.
             */
            float gainAdjustment = 1.0f;

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

            /**
             * @brief Scales envelope points when attach points change.
             * Envelope points in the fade-in region (cueStart to attachFrom) scale with attachFrom changes.
             * Envelope points in the fade-out region (attachTo to cueEndActual) scale with attachTo changes.
             * Points in the middle remain unaffected.
             *
             * @param oldAttachFrom Previous attachFrom value
             * @param newAttachFrom New attachFrom value
             * @param oldAttachTo Previous attachTo value
             * @param newAttachTo New attachTo value
             * @param trackDuration Natural duration of the source audio file
             */
            void scaleEnvelopePointsForAttachChange(
                Duration_t oldAttachFrom,
                Duration_t newAttachFrom,
                Duration_t oldAttachTo,
                Duration_t newAttachTo,
                Duration_t trackDuration)
            {
                const auto cueEndActual = getCueEndActual(trackDuration);

                spdlog::info("[ENVELOPE-SCALE] === Starting envelope scaling ===");
                spdlog::info("[ENVELOPE-SCALE] Track duration: {}ms, cueStart: {}ms, cueEnd: {}ms, cueEndActual: {}ms",
                           trackDuration.count(), cueStart.count(), cueEnd.count(), cueEndActual.count());
                spdlog::info("[ENVELOPE-SCALE] oldAttachFrom: {}ms -> newAttachFrom: {}ms",
                           oldAttachFrom.count(), newAttachFrom.count());
                spdlog::info("[ENVELOPE-SCALE] oldAttachTo: {}ms -> newAttachTo: {}ms",
                           oldAttachTo.count(), newAttachTo.count());
                spdlog::info("[ENVELOPE-SCALE] Starting with {} envelope points", envelopePoints.size());

                int fadeInCount = 0, fadeOutCount = 0, middleCount = 0;

                for (size_t i = 0; i < envelopePoints.size(); ++i)
                {
                    auto& point = envelopePoints[i];

                    // Scale fade-in points (between cueStart and attachFrom)
                    if (point.time >= cueStart && point.time <= oldAttachFrom)
                    {
                        fadeInCount++;
                        const auto fadeInRange = oldAttachFrom - cueStart;
                        spdlog::info("[ENVELOPE-SCALE] Point[{}] FADE-IN: time={}ms, volume={}, fadeInRange={}ms",
                                   i, point.time.count(), point.volume, fadeInRange.count());

                        if (fadeInRange.count() > 0)
                        {
                            const double ratio = static_cast<double>((point.time - cueStart).count()) / fadeInRange.count();
                            const auto newFadeInRange = newAttachFrom - cueStart;
                            point.time = cueStart + Duration_t{static_cast<int64_t>(ratio * newFadeInRange.count())};
                            spdlog::info("[ENVELOPE-SCALE]   Scaled: ratio={:.3f}, newFadeInRange={}ms, newTime={}ms",
                                       ratio, newFadeInRange.count(), point.time.count());
                        }
                        else
                        {
                            // Collapsed region - all points go to the boundary
                            point.time = newAttachFrom;
                            spdlog::info("[ENVELOPE-SCALE]   Collapsed to boundary: newTime={}ms", point.time.count());
                        }
                    }
                    // Scale fade-out points (between attachTo and cueEndActual)
                    else if (point.time >= oldAttachTo && point.time <= cueEndActual)
                    {
                        fadeOutCount++;
                        const auto fadeOutRange = cueEndActual - oldAttachTo;
                        spdlog::info("[ENVELOPE-SCALE] Point[{}] FADE-OUT: time={}ms, volume={}, fadeOutRange={}ms",
                                   i, point.time.count(), point.volume, fadeOutRange.count());

                        if (fadeOutRange.count() > 0)
                        {
                            const double ratio = static_cast<double>((point.time - oldAttachTo).count()) / fadeOutRange.count();
                            const auto newFadeOutRange = cueEndActual - newAttachTo;
                            point.time = newAttachTo + Duration_t{static_cast<int64_t>(ratio * newFadeOutRange.count())};
                            spdlog::info("[ENVELOPE-SCALE]   Scaled: ratio={:.3f}, newFadeOutRange={}ms, newTime={}ms",
                                       ratio, newFadeOutRange.count(), point.time.count());
                        }
                        else
                        {
                            // Collapsed region - all points go to the boundary
                            point.time = newAttachTo;
                            spdlog::info("[ENVELOPE-SCALE]   Collapsed to boundary: newTime={}ms", point.time.count());
                        }
                    }
                    else
                    {
                        // Points in the middle (between attachFrom and attachTo) remain unchanged
                        middleCount++;
                        spdlog::info("[ENVELOPE-SCALE] Point[{}] MIDDLE: time={}ms, volume={} (unchanged)",
                                   i, point.time.count(), point.volume);
                    }
                }

                spdlog::info("[ENVELOPE-SCALE] === Scaling complete: {} fade-in, {} middle, {} fade-out ===",
                           fadeInCount, middleCount, fadeOutCount);
            }
        };

        struct ExtendedMixInfo
        {
            MixInfo mixInfo;
            std::vector<MixTrack> tracks; // Detailed track info for the mix
        };


        /**
         * @brief How long a mix is, walked from its tracks.
         *
         * The ATTACH model, and the only implementation of it: each track starts where the previous
         * one's attachTo meets this one's attachFrom, and the mix ends where the last audible sample
         * does. Crossfades overlap, so the answer is always shorter than the sum of the durations.
         *
         * Here, in the model header, rather than in whichever component happened to need it. Six of
         * the eight paths that wrote a mix used to pass along whatever total they were holding, and
         * appending to a mix added the new tracks to the previous total instead of replacing it -
         * which is how a five-hour mix came to be stored as sixty-six hours. A second copy of this
         * walk would let the same thing happen again.
         *
         * @param tracks The mix, in order.
         * @param durationOf The natural length of a track, or nothing if the track cannot be resolved
         *        at all. Nothing is skipped - which is not the same as a track that resolves to a
         *        length of zero. Those exist, they are tracks whose length was never determined, and
         *        they still occupy their place because their attach points still position what follows.
         * @return The length of the finished mix; zero for an empty one.
         *
         * @note A skipped track still supplies the attach point for the track after it, because the
         *       predecessor is the previous element of the list rather than the last one successfully
         *       placed. That is not the more coherent of the two rules, but it is the one
         *       ExportMixImplementation already uses, and a length that disagreed with the audio the
         *       exporter produces would be worse than an odd rule consistently applied. MixPlaybackEngine
         *       is a third variation again; unifying all three is recorded in tasks.md and is not this
         *       change. Today no mix row in the library points at a track that cannot be resolved, so
         *       the three agree in practice.
         */
        template <typename DurationLookup> Duration_t calculateMixDuration(const std::vector<MixTrack> &tracks, DurationLookup durationOf)
        {
            Duration_t previousTrackStart{0};
            Duration_t mixEnd{0};

            for (size_t i = 0; i < tracks.size(); ++i)
            {
                const auto &track = tracks[i];
                const auto trackDuration = durationOf(track.trackId);
                if (!trackDuration.has_value())
                {
                    continue;
                }

                const auto trackStart{i == 0 ? Duration_t{0} : previousTrackStart + tracks[i - 1].attachTo - track.attachFrom};
                const auto trackEnd{trackStart + track.getEffectiveDuration(*trackDuration)};
                if (trackEnd > mixEnd)
                {
                    mixEnd = trackEnd;
                }

                previousTrackStart = trackStart;
            }

            return mixEnd;
        }

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
            if (!j.contains("time") || !j.contains("volume"))
            {
                spdlog::warn("EnvelopePoint JSON missing required fields, using defaults");
            }
            p.time = jucyaudio::Duration_t(j.value("time", int64_t{0}));
            p.volume = j.value("volume", jucyaudio::Volume_t{1000});
        }
    };

    template <> struct adl_serializer<jucyaudio::database::MixTrack>
    {
        static void to_json(json &j, const jucyaudio::database::MixTrack &mt)
        {
            // Only serialize the mix_data portion (not mixId, trackId, orderInMix)
            j = json{{"cue", {{"start", mt.cueStart.count()}, {"end", mt.cueEnd.count()}}},
                {"attach", {{"from", mt.attachFrom.count()}, {"to", mt.attachTo.count()}}},
                {"envelope", mt.envelopePoints},
                {"gainAdjustment", mt.gainAdjustment}};
        }

        static void from_json(const json &j, jucyaudio::database::MixTrack &mt)
        {
            // Use safe accessors with defaults for robustness against corrupted/incomplete JSON
            if (j.contains("cue") && j["cue"].is_object())
            {
                const auto& cue = j["cue"];
                mt.cueStart = jucyaudio::Duration_t(cue.value("start", int64_t{0}));
                mt.cueEnd = jucyaudio::Duration_t(cue.value("end", int64_t{0}));
            }
            else
            {
                spdlog::warn("MixTrack JSON missing 'cue' section, using defaults");
            }

            if (j.contains("attach") && j["attach"].is_object())
            {
                const auto& attach = j["attach"];
                mt.attachFrom = jucyaudio::Duration_t(attach.value("from", int64_t{0}));
                mt.attachTo = jucyaudio::Duration_t(attach.value("to", int64_t{0}));
            }
            else
            {
                spdlog::warn("MixTrack JSON missing 'attach' section, using defaults");
            }

            if (j.contains("envelope") && j["envelope"].is_array())
            {
                mt.envelopePoints = j["envelope"].get<std::vector<jucyaudio::database::EnvelopePoint>>();
            }
            else
            {
                spdlog::warn("MixTrack JSON missing 'envelope' section, using empty envelope");
            }

            mt.gainAdjustment = j.value("gainAdjustment", 1.0f);
        }
    };
} // namespace nlohmann
