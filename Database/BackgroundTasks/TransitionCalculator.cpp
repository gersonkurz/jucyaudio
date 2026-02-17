/*
 * This file is part of jucyaudio.
 * Copyright (C) 2025 Gerson Kurz <not@p-nand-q.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <Database/BackgroundTasks/TransitionCalculator.h>
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>
#include <limits>

namespace jucyaudio::database::background_tasks
{
    float TransitionCalculator::getEnergyAt(const std::vector<float>& contour, int64_t timeMs)
    {
        if (contour.empty())
            return 0.0f;

        // Energy contour has 1 value per second
        const size_t index = static_cast<size_t>(timeMs / 1000);

        if (index >= contour.size())
            return contour.back();

        return contour[index];
    }

    bool TransitionCalculator::isNearPhraseBoundary(
        const std::vector<Duration_t>& boundaries,
        int64_t timeMs,
        int64_t toleranceMs)
    {
        for (const auto& boundary : boundaries)
        {
            if (std::abs(boundary.count() - timeMs) <= toleranceMs)
                return true;
        }
        return false;
    }

    int64_t TransitionCalculator::snapToPhraseBoundary(
        const std::vector<Duration_t>& boundaries,
        int64_t timeMs,
        int64_t toleranceMs)
    {
        int64_t closestBoundary = timeMs;
        int64_t minDistance = toleranceMs + 1; // Start beyond tolerance

        for (const auto& boundary : boundaries)
        {
            const int64_t distance = std::abs(boundary.count() - timeMs);
            if (distance < minDistance)
            {
                minDistance = distance;
                closestBoundary = boundary.count();
            }
        }

        return (minDistance <= toleranceMs) ? closestBoundary : timeMs;
    }

    Duration_t TransitionCalculator::calculateCrossfadeDuration(float energyDiff, float avgEnergy)
    {
        // Both tracks quiet - use long crossfade for smooth ambient blending
        if (avgEnergy < LOW_ENERGY_THRESHOLD)
        {
            return Duration_t{CROSSFADE_LONG_MS};
        }

        // Good energy match - short crossfade
        if (energyDiff < ENERGY_DIFF_GOOD)
        {
            return Duration_t{CROSSFADE_SHORT_MS};
        }

        // Moderate mismatch - medium crossfade
        if (energyDiff < ENERGY_DIFF_MODERATE)
        {
            return Duration_t{CROSSFADE_MEDIUM_MS};
        }

        // Large mismatch - long crossfade to smooth it out
        return Duration_t{CROSSFADE_LONG_MS};
    }

    float TransitionCalculator::scoreCandidate(float energyDiff, bool nearPhraseA, bool nearPhraseB)
    {
        // Energy match score: 1.0 for perfect match, 0.0 for large difference
        const float energyScore = std::max(0.0f, 1.0f - energyDiff);

        // Phrase alignment score: 1.0 if both near boundaries, 0.5 if one, 0.0 if neither
        float phraseScore = 0.0f;
        if (nearPhraseA && nearPhraseB)
            phraseScore = 1.0f;
        else if (nearPhraseA || nearPhraseB)
            phraseScore = 0.5f;

        return energyScore * ENERGY_MATCH_WEIGHT + phraseScore * PHRASE_ALIGNMENT_WEIGHT;
    }

    TransitionResult TransitionCalculator::calculate(
        const EnergyAnalysisResult& energyA,
        Duration_t durationA,
        const EnergyAnalysisResult& energyB,
        Duration_t durationB)
    {
        TransitionResult result;

        // Validate inputs
        if (!energyA.isValid || !energyB.isValid ||
            energyA.energyContour.empty() || energyB.energyContour.empty())
        {
            spdlog::warn("TransitionCalculator: Invalid energy data, using fallback");
            return calculateFallback(durationA, durationB, Duration_t{5000});
        }

        // Define search regions
        // Track A: search in the outro region (from outroStart to end)
        const int64_t searchStartA = energyA.outroStart.count();
        const int64_t searchEndA = durationA.count();

        // Track B: search in the intro region (from start to introEnd)
        const int64_t searchStartB = 0;
        const int64_t searchEndB = energyB.introEnd.count();

        // Search step: 1 second (matching energy contour resolution)
        constexpr int64_t SEARCH_STEP_MS = 1000;

        float bestScore = -1.0f;
        int64_t bestPointA = searchStartA;
        int64_t bestPointB = searchStartB;
        float bestEnergyDiff = 1.0f;
        float bestAvgEnergy = 0.0f;

        // Search for best matching points
        for (int64_t pointA = searchStartA; pointA < searchEndA; pointA += SEARCH_STEP_MS)
        {
            const float energyAtA = getEnergyAt(energyA.energyContour, pointA);
            const bool nearPhraseA = isNearPhraseBoundary(energyA.phraseBoundaries, pointA, PHRASE_SNAP_TOLERANCE_MS);

            for (int64_t pointB = searchStartB; pointB < searchEndB; pointB += SEARCH_STEP_MS)
            {
                const float energyAtB = getEnergyAt(energyB.energyContour, pointB);
                const bool nearPhraseB = isNearPhraseBoundary(energyB.phraseBoundaries, pointB, PHRASE_SNAP_TOLERANCE_MS);

                const float energyDiff = std::abs(energyAtA - energyAtB);
                const float score = scoreCandidate(energyDiff, nearPhraseA, nearPhraseB);

                if (score > bestScore)
                {
                    bestScore = score;
                    bestPointA = pointA;
                    bestPointB = pointB;
                    bestEnergyDiff = energyDiff;
                    bestAvgEnergy = (energyAtA + energyAtB) / 2.0f;
                }
            }
        }

        // Snap to phrase boundaries if close
        const int64_t snappedPointA = snapToPhraseBoundary(energyA.phraseBoundaries, bestPointA, PHRASE_SNAP_TOLERANCE_MS);
        const int64_t snappedPointB = snapToPhraseBoundary(energyB.phraseBoundaries, bestPointB, PHRASE_SNAP_TOLERANCE_MS);
        const bool didSnap = (snappedPointA != bestPointA) || (snappedPointB != bestPointB);

        // Calculate crossfade duration based on energy characteristics
        const Duration_t crossfadeDuration = calculateCrossfadeDuration(bestEnergyDiff, bestAvgEnergy);

        // Ensure attach points stay within valid bounds
        // attachToA: clamp to [outroStart, duration - 1s] to stay in outro zone
        // attachFromB: clamp to [0, introEnd] to stay in intro zone
        const int64_t minAttachToA = energyA.outroStart.count();
        const int64_t maxAttachToA = durationA.count() - 1000;
        const int64_t safeMaxAttachToA = std::max<int64_t>(minAttachToA, maxAttachToA);
        result.attachToA = Duration_t{std::clamp(snappedPointA, minAttachToA, safeMaxAttachToA)};

        const int64_t maxAttachFromB = energyB.introEnd.count();
        const int64_t safeMaxAttachFromB = std::max<int64_t>(int64_t{0}, maxAttachFromB);
        result.attachFromB = Duration_t{std::clamp(snappedPointB, int64_t{0}, safeMaxAttachFromB)};
        result.crossfadeDuration = crossfadeDuration;
        result.score = bestScore;
        result.isValid = true;

        // Debug info
        result.energyAtCrossover = bestAvgEnergy;
        result.energyDifference = bestEnergyDiff;
        result.snappedToPhrase = didSnap;

        spdlog::debug("TransitionCalculator: A.attachTo={}ms, B.attachFrom={}ms, crossfade={}ms, "
                      "score={:.2f}, energyDiff={:.2f}, snapped={}",
                      result.attachToA.count(), result.attachFromB.count(),
                      result.crossfadeDuration.count(),
                      result.score, result.energyDifference, result.snappedToPhrase);

        return result;
    }

    TransitionResult TransitionCalculator::calculateFallback(
        Duration_t durationA,
        Duration_t durationB,
        Duration_t defaultCrossfade)
    {
        TransitionResult result;

        // Simple fallback: use fixed crossfade at end/start of tracks
        // Ensure crossfade doesn't exceed track durations
        const int64_t maxCrossfadeA = durationA.count() / 4; // Max 25% of track A
        const int64_t maxCrossfadeB = durationB.count() / 4; // Max 25% of track B
        const int64_t effectiveCrossfade = std::min({
            defaultCrossfade.count(),
            maxCrossfadeA,
            maxCrossfadeB
        });

        result.attachToA = Duration_t{durationA.count() - effectiveCrossfade};
        result.attachFromB = Duration_t{effectiveCrossfade};
        result.crossfadeDuration = Duration_t{effectiveCrossfade};
        result.score = 0.5f; // Neutral score for fallback
        result.isValid = true;
        result.snappedToPhrase = false;

        spdlog::debug("TransitionCalculator (fallback): A.attachTo={}ms, B.attachFrom={}ms, crossfade={}ms",
                      result.attachToA.count(), result.attachFromB.count(),
                      result.crossfadeDuration.count());

        return result;
    }

} // namespace jucyaudio::database::background_tasks
