#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/ITrackDatabase.h>
#include <filesystem>

namespace juce {
    template <typename FloatType>
    class AudioBuffer;
}

namespace jucyaudio
{
    namespace database
    {
        namespace background_tasks
        {
            // This function is now shared between BpmAnalysis and BpmAnalysisTask
            AudioMetadata analyzeAudioFile(const std::filesystem::path &filepath);

            // This function performs the analysis on an in-memory buffer
            AudioMetadata analyzeAudioBuffer(const juce::AudioBuffer<float>& buffer, double sampleRate);

        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio
