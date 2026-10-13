#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <Database/Includes/Constants.h>
#include <Database/Includes/IBackgroundTask.h>
#include <Database/Includes/IRefCounted.h>

namespace juce
{
    class AudioFormatManager;
}

namespace jucyaudio
{
    namespace database
    {
        class TrackLibrary;
        namespace background_tasks
        {
            /// @brief One step of the background analyzer: analyze the next track it has not analyzed, and record the outcome.
            /// @details Decoded: the BPM is stored, 0 if no beat was found, and the track is ok. Not decoded, or
            ///          no file: marked bad format. Either way the track is not picked again.
            /// @return False if no track was left to analyze.
            bool analyzeNextTrack(juce::AudioFormatManager &formats);

            struct BpmAnalysis final : public IBackgroundTask
            {
                explicit BpmAnalysis()
                    : IBackgroundTask{"BPM Analysis Task"}
                {
                }

            private:
                /// @brief Process the work for the BPM analysis task.
                void processWork() override;

                std::optional<std::chrono::steady_clock::time_point> m_startTime;

                /// @brief The file path to the audio file to analyze.
                std::string audioFilePath;

                /// @brief The BPM value found during the analysis.
                std::optional<double> bpmValue;
            };
        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio
