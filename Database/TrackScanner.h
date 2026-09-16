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

            /// @param scannedRoots If given, receives the roots that were actually walked. A scan can
            ///        skip a root - one whose path cannot be reconstructed, or one that is gone, not a
            ///        directory, or not listable - and still succeed, because the roots that were
            ///        present really were scanned and their files really are accounted for. The
            ///        caller needs to know which is which: the alternative is stamping a root that
            ///        nobody looked at as freshly scanned.
            ///
            ///        Two conditions on it. Pass an empty set - ids are inserted, never cleared. And
            ///        read it only when this returns true: an id goes in when its root is accepted,
            ///        before the walk of that root finishes, so after a failure the set holds the
            ///        roots that were started rather than the ones that were completed. On failure
            ///        there is no per-root answer to be had, which is why the one caller records
            ///        nothing at all then.
            bool scan(const std::vector<FolderId> &folderIdsToScan,
                bool forceRescanAllFiles,
                bool removeMissingFiles,
                ProgressCallback progressCb,
                CompletionCallback completionCb,
                std::atomic<bool> *shouldCancel,
                std::unordered_set<FolderId> *scannedRoots = nullptr);

        private:
            bool scanLoop(const std::vector<FolderId> &foldersToScan, std::unordered_set<FolderId> *scannedRoots);

            ITrackDatabase &m_db;

            std::vector<std::unique_ptr<ITrackInfoScanner>> m_scanners;

            ProgressCallback m_progressCb{nullptr};
            CompletionCallback m_completionCb{nullptr};
            std::atomic<bool> *m_pShouldCancel{nullptr};
            bool m_forceRescanAll{false};
            bool m_removeMissingFiles{false};
        };

    } // namespace database
} // namespace jucyaudio