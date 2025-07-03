#pragma once
#include <Database/Includes/ILongRunningTask.h>
#include <Database/Includes/ITrackDatabase.h>
#include <Database/Includes/ITrackInfoScanner.h>
#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>


namespace jucyaudio
{
    namespace database
    {

        class TrackScanner final
        {
        public:
            // Non-owning pointer to the database, must outlive TrackScanner or
            // be managed carefully
            TrackScanner(ITrackDatabase &database);
            ~TrackScanner() = default;

            TrackScanner(const TrackScanner &) = delete;
            TrackScanner &operator=(const TrackScanner &) = delete;

            bool scan(std::vector<FolderInfo> &foldersToScan,
                      bool forceRescanAllFile, ProgressCallback progressCb,
                      CompletionCallback completionCb,
                      std::atomic<bool> *shouldCancel);

        private:
            bool scanLoop(std::vector<FolderInfo> &foldersToScan);
            
            ITrackDatabase &m_db;

            std::vector<ITrackInfoScanner *> m_scanners;

            ProgressCallback m_progressCb{nullptr};
            CompletionCallback m_completionCb{nullptr};
            std::atomic<bool> *m_pShouldCancel{nullptr};
            bool m_forceRescanAll{false};
        };

    } // namespace database
} // namespace jucyaudio