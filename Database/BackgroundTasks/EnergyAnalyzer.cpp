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

#include <Database/BackgroundTasks/EnergyAnalyzer.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>
#include <UI/Settings.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <spdlog/spdlog.h>
#include <cmath>
#include <chrono>
#include <future>
#include <limits>

namespace jucyaudio::database::background_tasks
{
    // Maximum samples per channel we'll load (~45 minutes at 48kHz, ~1GB for stereo)
    constexpr int64_t MAX_SAMPLES_FOR_ANALYSIS = 200'000'000;

    namespace
    {
        std::vector<unsigned char> createWaveformBlobFromBuffer(const juce::AudioBuffer<float>& audioBuffer,
                                                                double sampleRate)
        {
            std::vector<unsigned char> blob;
            if (audioBuffer.getNumChannels() <= 0 || audioBuffer.getNumSamples() <= 0)
                return blob;

            juce::AudioFormatManager formatManager;
            formatManager.registerBasicFormats();

            juce::AudioThumbnailCache thumbnailCache{1};
            juce::AudioThumbnail thumbnail{512, formatManager, thumbnailCache};
            thumbnail.reset(audioBuffer.getNumChannels(),
                            sampleRate,
                            static_cast<juce::int64>(audioBuffer.getNumSamples()));

            constexpr int blockSize = 32768;
            for (int start = 0; start < audioBuffer.getNumSamples(); start += blockSize)
            {
                const int numToAdd = std::min(blockSize, audioBuffer.getNumSamples() - start);
                thumbnail.addBlock(static_cast<juce::int64>(start), audioBuffer, start, numToAdd);
            }

            juce::MemoryOutputStream stream;
            thumbnail.saveTo(stream);
            if (stream.getDataSize() == 0)
                return blob;

            const auto* data = static_cast<const unsigned char*>(stream.getData());
            blob.assign(data, data + stream.getDataSize());
            return blob;
        }
    } // namespace

    // --- EnergyAnalysisResult JSON serialization ---

    nlohmann::json EnergyAnalysisResult::toJson() const
    {
        nlohmann::json j;
        j["version"] = version;
        j["energy_contour"] = energyContour;

        // Convert phrase boundaries to milliseconds for JSON storage
        std::vector<int64_t> phraseBoundariesMs;
        phraseBoundariesMs.reserve(phraseBoundaries.size());
        for (const auto& pb : phraseBoundaries)
        {
            phraseBoundariesMs.push_back(pb.count());
        }
        j["phrase_boundaries"] = phraseBoundariesMs;
        j["analysis_timestamp"] = analysisTimestamp;

        return j;
    }

    EnergyAnalysisResult EnergyAnalysisResult::fromJson(const nlohmann::json& j)
    {
        EnergyAnalysisResult result;

        // Version is required
        if (!j.contains("version"))
            return result;
        result.version = j["version"].get<int>();

        // Energy contour is required and must be non-empty
        if (!j.contains("energy_contour"))
            return result;
        result.energyContour = j["energy_contour"].get<std::vector<float>>();
        if (result.energyContour.empty())
            return result;

        if (j.contains("phrase_boundaries"))
        {
            auto boundariesMs = j["phrase_boundaries"].get<std::vector<int64_t>>();
            result.phraseBoundaries.reserve(boundariesMs.size());
            for (auto ms : boundariesMs)
            {
                result.phraseBoundaries.emplace_back(ms);
            }
        }

        if (j.contains("analysis_timestamp"))
            result.analysisTimestamp = j["analysis_timestamp"].get<int64_t>();

        // Only mark as valid if we have the required data
        result.isValid = (result.version == ENERGY_ANALYSIS_VERSION && !result.energyContour.empty());
        return result;
    }

    std::optional<EnergyAnalysisResult> EnergyAnalysisResult::fromJsonString(const std::string& jsonStr)
    {
        if (jsonStr.empty())
            return std::nullopt;

        try
        {
            auto j = nlohmann::json::parse(jsonStr);
            auto result = fromJson(j);
            if (!result.isValid)
                return std::nullopt;
            return result;
        }
        catch (const nlohmann::json::exception& e)
        {
            spdlog::warn("Failed to parse energy analysis JSON: {}", e.what());
            return std::nullopt;
        }
    }

    // --- EnergyAnalyzer implementation ---

    std::vector<float> EnergyAnalyzer::calculateEnergyContour(const juce::AudioBuffer<float>& buffer,
                                                              double sampleRate)
    {
        std::vector<float> contour;

        const int samplesPerWindow = static_cast<int>(sampleRate * WINDOW_SIZE_SECONDS);
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        if (numSamples == 0 || samplesPerWindow == 0)
            return contour;

        const int numWindows = (numSamples + samplesPerWindow - 1) / samplesPerWindow;
        contour.reserve(numWindows);

        float maxRms = 0.0f;

        // First pass: calculate RMS for each window
        for (int window = 0; window < numWindows; ++window)
        {
            const int startSample = window * samplesPerWindow;
            const int endSample = std::min(startSample + samplesPerWindow, numSamples);
            const int windowLength = endSample - startSample;

            double sumSquared = 0.0;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* channelData = buffer.getReadPointer(ch);
                for (int i = startSample; i < endSample; ++i)
                {
                    const float sample = channelData[i];
                    sumSquared += sample * sample;
                }
            }

            const float rms = static_cast<float>(std::sqrt(sumSquared / (windowLength * numChannels)));
            contour.push_back(rms);

            if (rms > maxRms)
                maxRms = rms;
        }

        // Second pass: normalize by peak RMS
        if (maxRms > 0.0f)
        {
            for (float& value : contour)
            {
                value /= maxRms;
            }
        }

        return contour;
    }

    std::vector<Duration_t> EnergyAnalyzer::detectPhraseBoundaries(const std::vector<float>& energyContour,
                                                                   int windowSizeMs)
    {
        std::vector<Duration_t> boundaries;

        if (energyContour.size() < 3)
            return boundaries;

        // Calculate silence threshold in linear scale
        const float silenceThresholdLinear = std::pow(10.0f, SILENCE_THRESHOLD_DB / 20.0f);

        int64_t lastBoundaryMs = -MIN_PHRASE_SPACING_MS; // Allow first boundary at start

        for (size_t i = 1; i < energyContour.size() - 1; ++i)
        {
            const int64_t currentTimeMs = static_cast<int64_t>(i) * windowSizeMs;

            // Skip if too close to last boundary
            if (currentTimeMs - lastBoundaryMs < MIN_PHRASE_SPACING_MS)
                continue;

            const float current = energyContour[i];
            const float previous = energyContour[i - 1];

            // Check for silence
            if (current < silenceThresholdLinear && previous >= silenceThresholdLinear)
            {
                boundaries.emplace_back(currentTimeMs);
                lastBoundaryMs = currentTimeMs;
                continue;
            }

            // Check for significant energy change (>30% delta over 2-3 seconds window)
            // We look at a window of 2-3 values (seconds) around current position
            if (i >= 2 && i < energyContour.size() - 2)
            {
                // Calculate average energy in preceding 2-3 seconds
                float prevAvg = 0.0f;
                int prevCount = 0;
                for (size_t j = i - std::min(size_t(3), i); j < i; ++j)
                {
                    prevAvg += energyContour[j];
                    ++prevCount;
                }
                if (prevCount > 0)
                    prevAvg /= prevCount;

                // Calculate average energy in following 2-3 seconds
                float nextAvg = 0.0f;
                int nextCount = 0;
                for (size_t j = i; j < std::min(i + 3, energyContour.size()); ++j)
                {
                    nextAvg += energyContour[j];
                    ++nextCount;
                }
                if (nextCount > 0)
                    nextAvg /= nextCount;

                // Check for significant change
                const float maxVal = std::max(prevAvg, nextAvg);
                if (maxVal > 0.0f)
                {
                    const float delta = std::abs(nextAvg - prevAvg) / maxVal;
                    if (delta >= PHRASE_DELTA_THRESHOLD)
                    {
                        boundaries.emplace_back(currentTimeMs);
                        lastBoundaryMs = currentTimeMs;
                    }
                }
            }
        }

        return boundaries;
    }

    Duration_t EnergyAnalyzer::calculateIntroEnd(const std::vector<float>& energyContour,
                                                 int windowSizeMs,
                                                 Duration_t trackDuration)
    {
        if (energyContour.empty())
            return Duration_t{static_cast<int64_t>(trackDuration.count() * FALLBACK_PERCENTAGE)};

        // Calculate average energy of the full track
        float avgEnergy = 0.0f;
        for (const float& e : energyContour)
        {
            avgEnergy += e;
        }
        avgEnergy /= static_cast<float>(energyContour.size());

        const float threshold = avgEnergy * ENERGY_THRESHOLD;

        // Search in the first 25% of the track, capped by config
        const int64_t maxSearchMs = static_cast<int64_t>(
            config::theSettings.mixEditingSettings.smartAutomixMaxSearchSeconds.get()) * 1000;
        const int64_t percentSearchMs = static_cast<int64_t>(trackDuration.count() * INTRO_SEARCH_RANGE);
        const int64_t searchRangeMs = std::min(maxSearchMs, percentSearchMs);
        const size_t searchLimit = std::min(
            energyContour.size(),
            static_cast<size_t>(searchRangeMs / windowSizeMs));

        for (size_t i = 0; i < searchLimit && i < energyContour.size(); ++i)
        {
            if (energyContour[i] >= threshold)
            {
                return Duration_t{static_cast<int64_t>(i) * windowSizeMs};
            }
        }

        // Fallback: return 15% of track duration
        return Duration_t{static_cast<int64_t>(trackDuration.count() * FALLBACK_PERCENTAGE)};
    }

    Duration_t EnergyAnalyzer::calculateOutroStart(const std::vector<float>& energyContour,
                                                  int windowSizeMs,
                                                  Duration_t trackDuration)
    {
        if (energyContour.empty())
            return Duration_t{static_cast<int64_t>(trackDuration.count() * (1.0f - FALLBACK_PERCENTAGE))};

        // Calculate average energy of the full track
        float avgEnergy = 0.0f;
        for (const float& e : energyContour)
        {
            avgEnergy += e;
        }
        avgEnergy /= static_cast<float>(energyContour.size());

        const float threshold = avgEnergy * ENERGY_THRESHOLD;

        // Search forward from the capped start of the last 25% of the track
        const int64_t maxSearchMs = static_cast<int64_t>(
            config::theSettings.mixEditingSettings.smartAutomixMaxSearchSeconds.get()) * 1000;
        const int64_t percentSearchMs = static_cast<int64_t>(trackDuration.count() * OUTRO_SEARCH_RANGE);
        const int64_t searchRangeMs = std::min(maxSearchMs, percentSearchMs);
        const size_t searchWindows = std::min(
            energyContour.size(),
            static_cast<size_t>(searchRangeMs / windowSizeMs));
        const size_t searchStart = (energyContour.size() > searchWindows)
            ? (energyContour.size() - searchWindows)
            : 0;

        for (size_t i = searchStart; i < energyContour.size(); ++i)
        {
            if (energyContour[i] < threshold)
            {
                return Duration_t{static_cast<int64_t>(i) * windowSizeMs};
            }
        }

        // Fallback: return 85% of track duration
        return Duration_t{static_cast<int64_t>(trackDuration.count() * (1.0f - FALLBACK_PERCENTAGE))};
    }

    EnergyAnalysisResult EnergyAnalyzer::analyzeBuffer(const juce::AudioBuffer<float>& buffer,
                                                       double sampleRate,
                                                       Duration_t trackDuration)
    {
        EnergyAnalysisResult result;

        if (buffer.getNumSamples() == 0)
        {
            spdlog::warn("EnergyAnalyzer: Empty audio buffer");
            return result;
        }

        // Calculate energy contour (1 value per second)
        result.energyContour = calculateEnergyContour(buffer, sampleRate);

        if (result.energyContour.empty())
        {
            spdlog::warn("EnergyAnalyzer: Failed to calculate energy contour");
            return result;
        }

        const int windowSizeMs = WINDOW_SIZE_SECONDS * 1000;

        // Detect phrase boundaries
        result.phraseBoundaries = detectPhraseBoundaries(result.energyContour, windowSizeMs);

        // Calculate intro end and outro start
        result.introEnd = calculateIntroEnd(result.energyContour, windowSizeMs, trackDuration);
        result.outroStart = calculateOutroStart(result.energyContour, windowSizeMs, trackDuration);

        // Set analysis timestamp
        result.analysisTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();

        result.isValid = true;

        spdlog::debug("EnergyAnalyzer: contour size={}, phrases={}, intro_end={}ms, outro_start={}ms",
                      result.energyContour.size(),
                      result.phraseBoundaries.size(),
                      result.introEnd.count(),
                      result.outroStart.count());

        return result;
    }

    EnergyAnalysisResult EnergyAnalyzer::analyzeFile(const std::filesystem::path& filepath)
    {
        return analyzeFile(filepath, nullptr);
    }

    EnergyAnalysisResult EnergyAnalyzer::analyzeFile(const std::filesystem::path& filepath,
                                                     std::vector<unsigned char>* waveformBlobOut)
    {
        EnergyAnalysisResult result;

        // Initialize JUCE audio format manager
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        // Load the audio file
        juce::File audioFile{ui::jucePathFromFs(filepath)};
        if (!audioFile.existsAsFile())
        {
            spdlog::error("EnergyAnalyzer: Audio file does not exist: {}", pathToString(filepath));
            return result;
        }

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(audioFile));
        if (!reader)
        {
            spdlog::error("EnergyAnalyzer: Could not create audio format reader for: {}", pathToString(filepath));
            return result;
        }

        const int64_t numSamples = reader->lengthInSamples;
        const double sampleRate = reader->sampleRate;
        const int numChannels = static_cast<int>(reader->numChannels);

        // Guard against overflow and excessive memory usage
        if (numSamples > MAX_SAMPLES_FOR_ANALYSIS)
        {
            spdlog::warn("EnergyAnalyzer: File '{}' too long ({} samples, max {}). Skipping.",
                         filepath.filename().string(), numSamples, MAX_SAMPLES_FOR_ANALYSIS);
            return result;
        }

        if (numSamples > std::numeric_limits<int>::max())
        {
            spdlog::error("EnergyAnalyzer: File '{}' exceeds int max samples ({}). Skipping.",
                          filepath.filename().string(), numSamples);
            return result;
        }

        // Calculate track duration
        const auto trackDurationMs = static_cast<int64_t>((numSamples * 1000.0) / sampleRate);
        const Duration_t trackDuration{trackDurationMs};

        spdlog::info("EnergyAnalyzer: Reading full file '{}' ({} samples, {:.1f}s)",
                     filepath.filename().string(),
                     numSamples,
                     trackDurationMs / 1000.0);

        juce::AudioBuffer<float> audioBuffer(numChannels, static_cast<int>(numSamples));
        reader->read(&audioBuffer, 0, static_cast<int>(numSamples), 0, true, true);

        if (waveformBlobOut)
        {
            auto energyFuture = std::async(std::launch::async, [&audioBuffer, sampleRate, trackDuration]()
            {
                return analyzeBuffer(audioBuffer, sampleRate, trackDuration);
            });

            auto waveformFuture = std::async(std::launch::async, [&audioBuffer, sampleRate]()
            {
                return createWaveformBlobFromBuffer(audioBuffer, sampleRate);
            });

            result = energyFuture.get();
            *waveformBlobOut = waveformFuture.get();
        }
        else
        {
            // Perform energy analysis
            result = analyzeBuffer(audioBuffer, sampleRate, trackDuration);
        }

        return result;
    }

    bool EnergyAnalyzer::hasValidCachedData(const TrackInfo& trackInfo)
    {
        // Require both JSON data AND intro/outro columns to be populated
        // This catches legacy rows where JSON exists but intro/outro are NULL
        if (trackInfo.beat_locations_json.empty())
            return false;

        if (!trackInfo.intro_end.has_value() || !trackInfo.outro_start.has_value())
            return false;

        auto cached = getCachedData(trackInfo);
        return cached.has_value() && cached->isValid;
    }

    std::optional<EnergyAnalysisResult> EnergyAnalyzer::getCachedData(const TrackInfo& trackInfo)
    {
        if (trackInfo.beat_locations_json.empty())
            return std::nullopt;

        // Require intro/outro to be present for valid cached data
        if (!trackInfo.intro_end.has_value() || !trackInfo.outro_start.has_value())
            return std::nullopt;

        auto result = EnergyAnalysisResult::fromJsonString(trackInfo.beat_locations_json);
        if (result && result->isValid)
        {
            // Populate intro/outro from the authoritative track info columns
            result->introEnd = *trackInfo.intro_end;
            result->outroStart = *trackInfo.outro_start;
        }

        return result;
    }

} // namespace jucyaudio::database::background_tasks
