#include <Database/Scanners/Id3TagScanner.h>
#include <Database/TrackScanner.h>
#include <Utils/AssortedUtils.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <spdlog/spdlog.h>
#include <unordered_map>

// Namespace for cache helpers, local to this file
namespace
{
    struct TrackCacheKey
    {
        jucyaudio::FolderId parentId;
        std::string normalizedFilename;

        bool operator==(const TrackCacheKey &other) const
        {
            return parentId == other.parentId && normalizedFilename == other.normalizedFilename;
        }
    };
} // namespace

namespace std
{
    template <> struct hash<TrackCacheKey>
    {
        size_t operator()(const TrackCacheKey &k) const
        {
            return hash<jucyaudio::FolderId>()(k.parentId) ^ (hash<string>()(k.normalizedFilename) << 1);
        }
    };
} // namespace std

namespace jucyaudio
{
    namespace database
    {
        TrackScanner::TrackScanner(ITrackDatabase &database)
            : m_db{database}
        {
            // Revert to using raw pointers as per project style.
            m_scanners.push_back(new scanners::Id3TagScanner{m_db.getTagManager()});
        }

        // NOTE: The TrackScanner now owns the raw pointers in m_scanners.
        // A proper RAII implementation would require a custom destructor.
        // For now, we will assume the lifetime of the TrackScanner is the lifetime of the app.
        // ~TrackScanner() { for (auto* s : m_scanners) delete s; }

        bool TrackScanner::scan(const std::vector<FolderId> &folderIdsToScan,
            bool forceRescanAllFiles,
            ProgressCallback progressCb,
            CompletionCallback completionCb,
            std::atomic<bool> *shouldCancel)
        {
            m_progressCb = progressCb;
            m_completionCb = completionCb;
            m_pShouldCancel = shouldCancel;
            m_forceRescanAll = forceRescanAllFiles;

            const auto success = scanLoop(folderIdsToScan);

            if (m_completionCb)
            {
                m_completionCb(success, success ? "Scan completed successfully." : "Scan failed or was cancelled.");
            }

            m_progressCb = nullptr;
            m_completionCb = nullptr;
            m_pShouldCancel = nullptr;
            return success;
        }

        bool TrackScanner::scanLoop(const std::vector<FolderId> &folderIdsToScan)
        {
            spdlog::info("Hierarchical scan loop started. Force rescan: {}", m_forceRescanAll);
            if (m_progressCb)
                m_progressCb(-1.0f, "Initializing scan...");

            std::unordered_map<TrackCacheKey, TrackInfo> existingTrackCache;
            spdlog::debug("Building cache of existing tracks for scan scope...");

            TrackQueryArgs args;
            args.folderIds = folderIdsToScan;
            args.recursive = true;
            args.usePaging = false;

            auto tracksInScope = m_db.getTracks(args);
            for (const auto &track : tracksInScope)
            {
                if (auto normalizedName = normalizeForCache(track.filename))
                {
                    existingTrackCache[{track.folderId, *normalizedName}] = track;
                }
            }
            spdlog::debug("Cache built with {} tracks.", existingTrackCache.size());

            int filesProcessedThisSession = 0;
            for (const auto &rootFolderId : folderIdsToScan)
            {
                if (m_pShouldCancel && *m_pShouldCancel)
                    return false;

                auto rootFolderPath = m_db.reconstructFullPath(rootFolderId);
                if (rootFolderPath.empty())
                    continue;

                spdlog::info("Scanning folder: {}", pathToString(rootFolderPath));
                juce::RangedDirectoryIterator iter(juce::File(pathToString(rootFolderPath)), true, "*.mp3;*.wav;*.flac;*.ogg", juce::File::findFiles);

                for (const auto &entry : iter)
                {
                    if (m_pShouldCancel && *m_pShouldCancel)
                        return false;

                    filesProcessedThisSession++;
                    if (m_progressCb && (filesProcessedThisSession % 100 == 0))
                    {
                        m_progressCb(-1.0f, std::format("Scanned {} files...", filesProcessedThisSession));
                    }

                    const juce::File &file = entry.getFile();
                    const std::filesystem::path fullPath(file.getFullPathName().toStdString());
                    const std::string filename = file.getFileName().toStdString();

                    FolderId parentFolderId = m_db.getFolderDatabase().findOrCreateFolderByPath(fullPath.parent_path());
                    if (parentFolderId == -1)
                        continue;

                    const auto normalizedFilename = normalizeForCache(filename);
                    if (!normalizedFilename)
                        continue;

                    TrackCacheKey key = {parentFolderId, *normalizedFilename};
                    TrackInfo currentTrackInfo;
                    bool needsFullAnalysis = true;

                    auto cacheHit = existingTrackCache.find(key);
                    if (cacheHit != existingTrackCache.end())
                    {
                        currentTrackInfo = cacheHit->second;
                        existingTrackCache.erase(cacheHit);

                        // JUCE's juce::Time returns milliseconds since epoch.
                        // Our timestampFromInt64 expects milliseconds. This is compatible.
                        auto fsLastModifiedMs = file.getLastModificationTime().toMilliseconds();

                        if (!m_forceRescanAll && timestampToInt64(currentTrackInfo.last_modified_fs) == fsLastModifiedMs &&
                            currentTrackInfo.filesize_bytes == static_cast<uint64_t>(file.getSize()))
                        {
                            needsFullAnalysis = false;
                        }
                    }
                    else
                    {
                        currentTrackInfo.filename = filename;
                        currentTrackInfo.folderId = parentFolderId;
                        currentTrackInfo.date_added = std::chrono::system_clock::now();
                    }

                    currentTrackInfo.last_modified_fs = timestampFromInt64(file.getLastModificationTime().toMilliseconds());
                    currentTrackInfo.filesize_bytes = file.getSize();
                    currentTrackInfo.is_missing = false;
                    currentTrackInfo.last_scanned = std::chrono::system_clock::now();

                    if (needsFullAnalysis)
                    {
                        for (auto *scanner : m_scanners)
                        {
                            scanner->processTrack(currentTrackInfo, fullPath);
                        }
                    }

                    if (!m_db.saveTrackInfo(currentTrackInfo).isOk())
                    {
                        spdlog::error("Failed to save track info for {}", filename);
                    }
                }
            }

            if (!existingTrackCache.empty())
            {
                spdlog::info("Found {} tracks in the database that are missing from the filesystem.", existingTrackCache.size());
                for (const auto &[key, track] : existingTrackCache)
                {
                    m_db.setTrackPathMissing(track.trackId, true);
                }
            }

            if (m_progressCb)
                m_progressCb(1.0f, "Scan complete.");
            spdlog::info("Scan loop finished.");
            return true;
        }
    } // namespace database
} // namespace jucyaudio