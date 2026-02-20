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
#include <Database/Includes/TrackInfo.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <optional>
#include <vector>

namespace juce
{
    template <typename>
    class AudioBuffer;
}

namespace jucyaudio::database::background_tasks
{
    constexpr int ENERGY_ANALYSIS_VERSION = 1;

    // Algorithm constants
    constexpr int WINDOW_SIZE_SECONDS = 1;
    constexpr float ENERGY_THRESHOLD = 0.6f;
    constexpr float PHRASE_DELTA_THRESHOLD = 0.3f;
    constexpr float SILENCE_THRESHOLD_DB = -40.0f;
    constexpr int MIN_PHRASE_SPACING_MS = 4000;
    constexpr float INTRO_SEARCH_RANGE = 0.25f;
    constexpr float OUTRO_SEARCH_RANGE = 0.25f;
    constexpr float FALLBACK_PERCENTAGE = 0.15f;

    struct EnergyAnalysisResult
    {
        int version = ENERGY_ANALYSIS_VERSION;
        std::vector<float> energyContour;         // RMS per second, normalized 0.0-1.0
        std::vector<Duration_t> phraseBoundaries; // Timestamps (ms)
        int64_t analysisTimestamp = 0;
        Duration_t introEnd{0};
        Duration_t outroStart{0};
        bool isValid = false;

        nlohmann::json toJson() const;
        static EnergyAnalysisResult fromJson(const nlohmann::json& j);
        static std::optional<EnergyAnalysisResult> fromJsonString(const std::string& jsonStr);
    };

    class EnergyAnalyzer
    {
    public:
        static EnergyAnalysisResult analyzeFile(const std::filesystem::path& filepath);
        static EnergyAnalysisResult analyzeFile(const std::filesystem::path& filepath,
                                                std::vector<unsigned char>* waveformBlobOut);
        static EnergyAnalysisResult analyzeBuffer(const juce::AudioBuffer<float>& buffer,
                                                  double sampleRate,
                                                  Duration_t trackDuration);
        static bool hasValidCachedData(const TrackInfo& trackInfo);
        static std::optional<EnergyAnalysisResult> getCachedData(const TrackInfo& trackInfo);

    private:
        static std::vector<float> calculateEnergyContour(const juce::AudioBuffer<float>& buffer,
                                                         double sampleRate);
        static std::vector<Duration_t> detectPhraseBoundaries(const std::vector<float>& energyContour,
                                                              int windowSizeMs);
        static Duration_t calculateIntroEnd(const std::vector<float>& energyContour,
                                            int windowSizeMs,
                                            Duration_t trackDuration);
        static Duration_t calculateOutroStart(const std::vector<float>& energyContour,
                                              int windowSizeMs,
                                              Duration_t trackDuration);
    };

} // namespace jucyaudio::database::background_tasks
