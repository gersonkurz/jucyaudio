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
            // This function performs the analysis on an in-memory buffer
            AudioMetadata analyzeAudioBuffer(const juce::AudioBuffer<float>& buffer, double sampleRate);

        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio
