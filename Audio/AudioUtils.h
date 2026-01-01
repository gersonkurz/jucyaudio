#pragma once

#include <Database/Includes/MixInfo.h>
#include <Database/Includes/Constants.h>
#include <vector>

namespace jucyaudio
{
    namespace audio
    {
        using namespace database;

        /**
         * @brief Interpolates volume from a set of envelope points for a given time.
         * 
         * @param envelopePoints The vector of envelope points sorted by time.
         * @param timeInTrack The time position within the track.
         * @return The interpolated volume factor (0.0 to 1.0 typically, but can be higher).
         */
        inline float interpolateVolumeFromEnvelope(const std::vector<EnvelopePoint> &envelopePoints, Duration_t timeInTrack)
        {
            if (envelopePoints.empty())
            {
                return 1.0f; // Default to full volume if no envelope points
            }

            // If before first point, use first point's volume
            if (timeInTrack <= envelopePoints.front().time)
            {
                return envelopePoints.front().volume / (float)VOLUME_NORMALIZATION;
            }

            // If after last point, use last point's volume
            if (timeInTrack >= envelopePoints.back().time)
            {
                return envelopePoints.back().volume / (float)VOLUME_NORMALIZATION;
            }

            // Find the two points to interpolate between
            for (size_t i = 0; i < envelopePoints.size() - 1; ++i)
            {
                const auto &pointA = envelopePoints[i];
                const auto &pointB = envelopePoints[i + 1];

                if (timeInTrack >= pointA.time && timeInTrack <= pointB.time)
                {
                    // Linear interpolation between the two points
                    float progress = 0.0f;
                    auto timeDiff = pointB.time - pointA.time;
                    if (timeDiff.count() > 0)
                    {
                        progress = (float)(timeInTrack - pointA.time).count() / (float)timeDiff.count();
                    }

                    float volumeA = pointA.volume / (float)VOLUME_NORMALIZATION;
                    float volumeB = pointB.volume / (float)VOLUME_NORMALIZATION;

                    return volumeA + progress * (volumeB - volumeA);
                }
            }

            // Fallback (shouldn't reach here)
            return 1.0f;
        }
    }
}
