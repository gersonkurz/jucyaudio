#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/MixInfo.h>
#include <algorithm>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        /**
         * @brief Indicates what crossfade adjustment was made for a track.
         */
        enum class CrossfadeAdjustment
        {
            None,       // Full crossfade used
            Eliminated, // Crossfade eliminated (very short track)
            Reduced     // Crossfade reduced (medium-short track)
        };

        /**
         * @brief Holds the calculated crossfade settings for a track in a mix.
         */
        struct CrossfadeSettings
        {
            Duration_t effectiveCrossfade{0};
            Duration_t attachFrom{0};
            Duration_t attachTo{0};
            std::vector<EnvelopePoint> envelopePoints;
            CrossfadeAdjustment adjustment{CrossfadeAdjustment::None};
        };

        /**
         * @brief Calculates crossfade settings for a track based on its duration.
         *
         * This handles:
         * - Very short tracks (< 2 * defaultCrossfade): no crossfade
         * - Medium-short tracks (< 4 * defaultCrossfade): reduced crossfade (10% of duration)
         * - Normal tracks: full crossfade
         *
         * @param trackDuration The total duration of the track.
         * @param defaultCrossfade The default crossfade duration to use for normal tracks.
         * @return CrossfadeSettings with calculated values and envelope points.
         */
        inline CrossfadeSettings calculateCrossfadeForTrack(Duration_t trackDuration, Duration_t defaultCrossfade)
        {
            CrossfadeSettings settings;

            const auto minimumExpectedSongLength = 2 * defaultCrossfade;
            settings.effectiveCrossfade = defaultCrossfade;
            settings.adjustment = CrossfadeAdjustment::None;

            if (trackDuration < minimumExpectedSongLength)
            {
                // Very short tracks: eliminate crossfade entirely
                settings.effectiveCrossfade = Duration_t{0};
                settings.adjustment = CrossfadeAdjustment::Eliminated;
            }
            else if (trackDuration < minimumExpectedSongLength * 2)
            {
                // Medium-short tracks: use reduced crossfade (10% of duration)
                settings.effectiveCrossfade = trackDuration / 10;
                settings.adjustment = CrossfadeAdjustment::Reduced;
            }

            // Set attach points based on effective crossfade
            settings.attachFrom = settings.effectiveCrossfade;
            settings.attachTo = trackDuration - settings.effectiveCrossfade;

            // Create envelope points for crossfade
            if (settings.effectiveCrossfade == Duration_t{0})
            {
                // No crossfade - track plays at full volume throughout
                settings.envelopePoints = {
                    {Duration_t{0}, VOLUME_NORMALIZATION},
                    {trackDuration, VOLUME_NORMALIZATION}
                };
            }
            else
            {
                // Calculate midpoints based on effective crossfade
                const auto fadeInMidpoint = std::min(Duration_t{2000}, settings.effectiveCrossfade / 2);
                const auto fadeOutMidpoint = trackDuration - std::min(Duration_t{2000}, settings.effectiveCrossfade / 2);

                settings.envelopePoints = {
                    {Duration_t{0}, Volume_t{200}},                                          // Start at 20%
                    {fadeInMidpoint, Volume_t{700}},                                         // midpoint: 70%
                    {settings.effectiveCrossfade, VOLUME_NORMALIZATION},                     // crossfade end: 100%
                    {trackDuration - settings.effectiveCrossfade, VOLUME_NORMALIZATION},    // before fade out: 100%
                    {fadeOutMidpoint, Volume_t{700}},                                        // midpoint: 70%
                    {trackDuration, Volume_t{200}}                                           // End at 20%
                };
            }

            return settings;
        }
    }
}
