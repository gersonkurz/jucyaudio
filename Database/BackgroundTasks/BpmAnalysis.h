#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <Database/Includes/Constants.h>
#include <Database/Includes/IBackgroundTask.h>
#include <Database/Includes/IRefCounted.h>

namespace jucyaudio
{
    namespace database
    {
        class TrackLibrary;
        namespace background_tasks
        {
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
