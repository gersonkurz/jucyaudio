#pragma once
#include <Database/Includes/Constants.h>
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
    struct TrackCacheKey
    {
        FolderId parentId;
        std::string normalizedFilename;

        bool operator==(const TrackCacheKey &other) const
        {
            return parentId == other.parentId && normalizedFilename == other.normalizedFilename;
        }
    };
} // namespace jucyaudio

namespace std
{
    template <> struct hash<jucyaudio::TrackCacheKey>
    {
        size_t operator()(const jucyaudio::TrackCacheKey &k) const
        {
            return hash<jucyaudio::FolderId>()(k.parentId) ^ (hash<string>()(k.normalizedFilename) << 1);
        }
    };
} // namespace std

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

            bool scan(const std::vector<FolderId> &folderIdsToScan,
                bool forceRescanAllFiles,
                ProgressCallback progressCb,
                CompletionCallback completionCb,
                std::atomic<bool> *shouldCancel);

        private:
            bool scanLoop(const std::vector<FolderId> &foldersToScan);
            
            ITrackDatabase &m_db;

            std::vector<ITrackInfoScanner *> m_scanners;

            ProgressCallback m_progressCb{nullptr};
            CompletionCallback m_completionCb{nullptr};
            std::atomic<bool> *m_pShouldCancel{nullptr};
            bool m_forceRescanAll{false};
        };

    } // namespace database
} // namespace jucyaudio