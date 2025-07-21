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
                explicit BpmAnalysisTask(std::vector<TrackId> trackIds);
                ~BpmAnalysisTask() override = default;

            private:
                void run(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel) override;
                void runInternal(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel);

                std::vector<TrackId> m_trackIds;
            };
        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio
