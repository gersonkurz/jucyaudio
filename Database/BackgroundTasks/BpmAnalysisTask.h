#pragma once

#include <Database/Includes/ILongRunningTask.h>
#include <Database/Includes/TrackInfo.h>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        namespace background_tasks
        {
            class BpmAnalysisTask final : public ILongRunningTask
            {
            public:
                explicit BpmAnalysisTask(std::vector<TrackInfo> trackInfos);
                ~BpmAnalysisTask() override = default;

                const std::vector<TrackInfo> &getBadFiles() const
                {
                    return m_badFiles;
                }

            private:
                void run(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel) override;
                void runInternal(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel);

                std::vector<TrackInfo> m_trackInfos;
                std::vector<TrackInfo> m_badFiles; // Tracks that failed to decode
            };
        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio
