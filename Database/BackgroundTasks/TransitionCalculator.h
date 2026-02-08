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

#pragma once

#include <Database/Includes/Constants.h>
#include <Database/BackgroundTasks/EnergyAnalyzer.h>
#include <optional>

namespace jucyaudio::database::background_tasks
{
    // Transition calculation constants
    constexpr float ENERGY_MATCH_WEIGHT = 0.7f;
    constexpr float PHRASE_ALIGNMENT_WEIGHT = 0.3f;
    constexpr int64_t PHRASE_SNAP_TOLERANCE_MS = 2000; // Snap to phrase within 2 seconds

    // Crossfade duration constants (milliseconds)
    constexpr int64_t CROSSFADE_SHORT_MS = 3000;   // Good energy match
    constexpr int64_t CROSSFADE_MEDIUM_MS = 6000;  // Moderate mismatch
    constexpr int64_t CROSSFADE_LONG_MS = 12000;   // Quiet/ambient blending

    // Energy thresholds for crossfade duration selection
    constexpr float ENERGY_DIFF_GOOD = 0.15f;      // <15% difference = short crossfade
    constexpr float ENERGY_DIFF_MODERATE = 0.35f;  // <35% difference = medium crossfade
    constexpr float LOW_ENERGY_THRESHOLD = 0.3f;   // Both tracks quiet = long crossfade

    /**
     * @brief Result of transition calculation between two tracks.
     */
    struct TransitionResult
    {
        Duration_t attachToA{0};       // AttachTo point for outgoing track A
        Duration_t attachFromB{0};     // AttachFrom point for incoming track B
        Duration_t crossfadeDuration{0};
        float score{0.0f};             // Quality score 0.0-1.0 (higher is better)
        bool isValid{false};

        // Debug info
        float energyAtCrossover{0.0f}; // Average energy level at transition point
        float energyDifference{0.0f};  // Energy difference between tracks at crossover
        bool snappedToPhrase{false};   // Whether we snapped to a phrase boundary
    };

    /**
     * @brief Calculates optimal transition points between two tracks based on energy analysis.
     *
     * The algorithm:
     * 1. Searches A's outro zone and B's intro zone for matching energy levels
     * 2. Scores candidates based on energy match (70%) and phrase alignment (30%)
     * 3. Selects best candidate and determines appropriate crossfade duration
     */
    class TransitionCalculator
    {
    public:
        /**
         * @brief Calculate optimal transition between two tracks.
         *
         * @param energyA Energy analysis result for outgoing track A
         * @param durationA Total duration of track A
         * @param energyB Energy analysis result for incoming track B
         * @param durationB Total duration of track B
         * @return TransitionResult with optimal attach points and crossfade duration
         */
        static TransitionResult calculate(
            const EnergyAnalysisResult& energyA,
            Duration_t durationA,
            const EnergyAnalysisResult& energyB,
            Duration_t durationB);

        /**
         * @brief Calculate transition using fallback (fixed duration) when energy data unavailable.
         *
         * @param durationA Total duration of track A
         * @param durationB Total duration of track B
         * @param defaultCrossfade Default crossfade duration to use
         * @return TransitionResult with simple fixed-duration crossfade
         */
        static TransitionResult calculateFallback(
            Duration_t durationA,
            Duration_t durationB,
            Duration_t defaultCrossfade);

    private:
        /**
         * @brief Get energy value at a specific time position.
         * @param contour Energy contour (1 value per second)
         * @param timeMs Time in milliseconds
         * @return Energy value (0.0-1.0) or 0.0 if out of bounds
         */
        static float getEnergyAt(const std::vector<float>& contour, int64_t timeMs);

        /**
         * @brief Check if a time position is near a phrase boundary.
         * @param boundaries List of phrase boundary timestamps
         * @param timeMs Time to check
         * @param toleranceMs How close counts as "near"
         * @return true if within tolerance of any boundary
         */
        static bool isNearPhraseBoundary(
            const std::vector<Duration_t>& boundaries,
            int64_t timeMs,
            int64_t toleranceMs);

        /**
         * @brief Snap a time position to the nearest phrase boundary if close enough.
         * @param boundaries List of phrase boundary timestamps
         * @param timeMs Time to potentially snap
         * @param toleranceMs Maximum distance to snap
         * @return Snapped time (or original if no boundary nearby)
         */
        static int64_t snapToPhraseBoundary(
            const std::vector<Duration_t>& boundaries,
            int64_t timeMs,
            int64_t toleranceMs);

        /**
         * @brief Calculate crossfade duration based on energy characteristics.
         * @param energyDiff Energy difference at crossover point
         * @param avgEnergy Average energy level at crossover
         * @return Appropriate crossfade duration
         */
        static Duration_t calculateCrossfadeDuration(float energyDiff, float avgEnergy);

        /**
         * @brief Score a candidate transition point.
         * @param energyDiff Energy difference (lower is better)
         * @param nearPhraseA Whether near phrase boundary in track A
         * @param nearPhraseB Whether near phrase boundary in track B
         * @return Score from 0.0 to 1.0
         */
        static float scoreCandidate(float energyDiff, bool nearPhraseA, bool nearPhraseB);
    };

} // namespace jucyaudio::database::background_tasks
