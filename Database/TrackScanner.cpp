#include <Database/Scanners/Id3TagScanner.h>
#include <Database/TrackScanner.h>
#include <Utils/AssortedUtils.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <unordered_set> 

namespace jucyaudio
{
    namespace database
    {
        TrackScanner::TrackScanner(ITrackDatabase &database)
            : m_db{database}
        {
            m_scanners.push_back(new scanners::Id3TagScanner{m_db.getTagManager()});
        }

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

            // --- THE FIX: A set to track files processed in this session ---
            std::unordered_set<std::string> processedPaths;

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

                    const juce::File &file = entry.getFile();
                    const auto fullPathStr = file.getFullPathName().toStdString();

                    // --- THE FIX: Check if we have already processed this exact file path ---
                    if (processedPaths.contains(fullPathStr))
                    {
                        continue; // Already handled, skip to the next file.
                    }
                    processedPaths.insert(fullPathStr); // Mark this path as processed for this session.

                    filesProcessedThisSession++;
                    if (m_progressCb && (filesProcessedThisSession % 100 == 0))
                    {
                        m_progressCb(-1.0f, std::format("Scanned {} files...", filesProcessedThisSession));
                    }

                    const std::filesystem::path fullPath(fullPathStr);
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