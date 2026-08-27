#include <Database/Scanners/Id3TagScanner.h>
#include <Database/TrackScanner.h>
#include <Utils/AssortedUtils.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <spdlog/spdlog.h>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace jucyaudio
{
    namespace database
    {
        TrackScanner::TrackScanner(ITrackDatabase &database)
            : m_db{database}
        {
            m_scanners.push_back(std::make_unique<scanners::Id3TagScanner>(m_db.getTagManager()));
        }

        bool TrackScanner::scan(const std::vector<FolderId> &folderIdsToScan,
            bool forceRescanAllFiles,
            bool removeMissingFiles,
            ProgressCallback progressCb,
            CompletionCallback completionCb,
            std::atomic<bool> *shouldCancel)
        {
            m_progressCb = progressCb;
            m_completionCb = completionCb;
            m_pShouldCancel = shouldCancel;
            m_forceRescanAll = forceRescanAllFiles;
            m_removeMissingFiles = removeMissingFiles;

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

            // this is a lookup of existing folders and their tracks by name.
            std::unordered_map<FolderId, std::unordered_map<std::string, TrackId>> existingTrackCache;
            std::unordered_set<std::string> existingFolders;

            // Tracks that were already flagged is_missing when this scan started. The cache above maps a
            // name to a bare TrackId and carries no other fields, so the flag has to be remembered here
            // rather than read back from it later. Used twice below: to clear the flag on a track whose
            // file has come back, and to avoid rewriting the flag onto rows that already carry it.
            std::unordered_set<TrackId> previouslyMissing;

            spdlog::debug("Building cache of existing tracks for scan scope...");

            const auto &folderDatabase{m_db.getFolderDatabase()};
            TrackQueryArgs args{};
            const auto folderSet{folderDatabase.getAllChildFolders(folderIdsToScan)};
            args.folderIds = std::vector<FolderId>{folderSet.begin(), folderSet.end()};
            args.recursive = true;
            args.usePaging = false;

            spdlog::info("Determining tracks in scope for the scan...");
            const auto tracksInScope{m_db.getTracks(args)};
            spdlog::info("Found {} tracks in the database for the scan scope.", tracksInScope.size());
            //uint64_t index = 0;
            for (const auto &track : tracksInScope)
            {
                // Every track in scope goes into the cache, missing ones included. They are needed here
                // for two different reasons: so they can be collected for removal when the user asked
                // for that, and so that a file which has come back is recognised as the track it always
                // was instead of looking like a new one - which is how a missing track used to become
                // permanently missing.
                if (track.is_missing)
                {
                    previouslyMissing.insert(track.trackId);
                }

                // register this folder as existing
                auto item = existingTrackCache.find(track.folderId);
                if (item == existingTrackCache.end())
                {
                    existingTrackCache[track.folderId] = {};
                    item = existingTrackCache.find(track.folderId);
                }
                assert(item != existingTrackCache.end() && "Track folder ID should exist in the cache now.");
                const auto key{normalizeForCache(track.filename)};
                item->second[key] = track.trackId;
                //++index;
            }

            spdlog::debug("Cache built with {} folders and {} tracks.", existingTrackCache.size(), tracksInScope.size());

            // Files already handled in THIS scan run, keyed by (folderId, normalized filename).
            // Overlapping roots (e.g. D:\MP3 and D:\MP3\DARKGAZE) reach the same physical folder
            // twice; without this guard the destructive existingTrackCache erase makes the second
            // encounter look new and insert a duplicate. The UNIQUE(folder_id, filename) index is
            // the hard backstop; this just avoids the wasted insert + failure log.
            std::set<std::pair<FolderId, std::string>> processedThisRun;

            int filesProcessedThisSession = 0;
            for (const auto &rootFolderId : folderIdsToScan)
            {
                if (m_pShouldCancel && *m_pShouldCancel)
                    return false;

                auto rootFolderPath = m_db.reconstructFullPath(rootFolderId);
                if (rootFolderPath.empty())
                    continue;

                spdlog::info("Scanning folder: {}", pathToString(rootFolderPath));
                juce::RangedDirectoryIterator iter{juce::File(pathToString(rootFolderPath)), true, "*.mp3;*.wav;*.flac;*.ogg", juce::File::findFiles};

                for (const auto &entry : iter)
                {
                    if (m_pShouldCancel && *m_pShouldCancel)
                        return false;

                    const juce::File &file = entry.getFile();
                    const auto fullPathStr = file.getFullPathName().toStdString();

                    filesProcessedThisSession++;
                    if (m_progressCb && (filesProcessedThisSession % 100 == 0))
                    {
                        m_progressCb(-1.0f, std::format("Scanned {:L} files...", filesProcessedThisSession));
                    }

                    // we're getting filenames, not folders
                    const std::filesystem::path fullPath{fullPathStr};
                    const std::string filename{file.getFileName().toStdString()};

                    // we identify the folder by its parent path
                    FolderId parentFolderId = m_db.getFolderDatabase().findOrCreateFolderByPath(fullPath.parent_path());
                    if (parentFolderId <= 0)
                        continue;

                    const auto normalizedFilename = normalizeForCache(filename);

                    // Skip if this physical file was already processed this run via another root.
                    if (!processedThisRun.insert({parentFolderId, normalizedFilename}).second)
                    {
                        continue;
                    }

                    // let's check if the folder existed before:
                    auto item = existingTrackCache.find(parentFolderId);
                    if (item == existingTrackCache.end())
                    {
                        const auto pathToStore{pathToString(fullPath.parent_path())};
                        const auto pathItem{existingFolders.find(pathToStore)};
                        if (pathItem != existingFolders.end())
                        {
                            spdlog::info("Folder {} is new - did not exist before", pathToStore);
                        }
                        spdlog::info("File {} is new in {}, processing.", file.getFileName().toStdString(), pathToStore);
                    }
                    else
                    {
                        // check if this file already exists in the folder
                        const auto known{item->second.find(normalizedFilename)};
                        if (known != item->second.end())
                        {
                            const auto knownTrackId{known->second};
                            item->second.erase(known);

                            // The file is back. Clear the flag here, before the early-out below: the
                            // ordinary scan is the un-forced one, so clearing after that continue would
                            // mean it effectively never ran. Nothing else about the track is touched -
                            // the row keeps its id, and with it every mix reference and working set
                            // membership that points at it.
                            if (previouslyMissing.contains(knownTrackId))
                            {
                                const auto result{m_db.setTrackPathMissing(knownTrackId, false)};
                                if (!result.isOk())
                                {
                                    spdlog::error("Failed to clear missing flag for track {} ({}): {}", knownTrackId, filename, result.errorMessage);
                                    return false;
                                }
                                spdlog::info("Track {} ({}) is back on disk; cleared its missing flag.", knownTrackId, filename);
                            }

                            //spdlog::debug("Track {} already exists in folder {}.", filename, pathToString(fullPath.parent_path()));
                            if (!m_forceRescanAll)
                            {
                                // If we're not forcing a rescan, skip this file
                                continue;
                            }
                        }
                        else
                        {
                            spdlog::debug("Track {} is new in folder {}, processing.", filename, pathToString(fullPath.parent_path()));
                        }
                    }

                    TrackInfo currentTrackInfo;
                    currentTrackInfo.filename = filename;
                    currentTrackInfo.folderId = parentFolderId;
                    currentTrackInfo.date_added = std::chrono::system_clock::now();
                    currentTrackInfo.last_modified_fs = timestampFromInt64(file.getLastModificationTime().toMilliseconds());
                    currentTrackInfo.filesize_bytes = file.getSize();
                    assert(currentTrackInfo.is_missing == false);
                    currentTrackInfo.last_scanned = std::chrono::system_clock::now();

                    for (const auto& scanner : m_scanners)
                    {
                        scanner->processTrack(currentTrackInfo, fullPath);
                    }

                    if (!m_db.saveTrackInfo(currentTrackInfo).isOk())
                    {
                        spdlog::error("Failed to save track info for {}", filename);
                    }
                }
            }

            // Count actual missing tracks (not folder buckets)
            size_t missingTrackCount = 0;
            for (const auto& item : existingTrackCache)
            {
                missingTrackCount += item.second.size();
            }

            if (missingTrackCount > 0)
            {
                spdlog::info("Found {} tracks in the database that are missing from the filesystem.", missingTrackCount);

                if (m_removeMissingFiles)
                {
                    spdlog::info("User opted to remove missing files. Collecting IDs for deletion...");

                    std::vector<TrackId> idsToDelete;
                    for (const auto& item : existingTrackCache)
                    {
                        for (const auto &track : item.second)
                        {
                            idsToDelete.push_back(track.second);
                        }
                    }
                    spdlog::info("Removing {} missing tracks from the database...", idsToDelete.size());

                    const auto removeTrackSuccess = m_db.removeTracks(idsToDelete);
                    if (removeTrackSuccess.isOk())
                    {
                        spdlog::info("Successfully removed {} missing tracks from the database.", idsToDelete.size());
                    }
                    else
                    {
                        spdlog::error("Failed to remove missing tracks from the database: {}", removeTrackSuccess.errorMessage);
                    }

                    // the database needs to check if the folders are empty. so:
                    const auto removeFoldersSuccess = folderDatabase.removeEmptyFolders();
                    if (removeFoldersSuccess)
                    {
                        spdlog::info("Successfully removed empty folders from the database.");
                    }
                    else
                    {
                        spdlog::error("Failed to remove empty folders from the database");
                    }
                }
                else
                {
                    // Only the ones that are newly missing: the leftovers now include tracks that were
                    // already flagged when the scan started, and rewriting a 1 over a 1 is thousands of
                    // pointless updates on a library this size.
                    size_t newlyMarked = 0;
                    for (const auto& item : existingTrackCache)
                    {
                        for (const auto& track : item.second)
                        {
                            if (previouslyMissing.contains(track.second))
                            {
                                continue;
                            }

                            const auto result{m_db.setTrackPathMissing(track.second, true)};
                            if (!result.isOk())
                            {
                                // Same reasoning as clearing the flag above: a scan that reports success
                                // while a track it knows is gone still says otherwise is worse than a
                                // scan that admits it failed. track.first is the normalized cache key,
                                // which is enough to identify the file.
                                spdlog::error("Failed to mark track {} ({}) as missing: {}", track.second, track.first, result.errorMessage);
                                return false;
                            }
                            ++newlyMarked;
                        }
                    }
                    spdlog::info("Marked {} newly missing files in the database ({} were already flagged).",
                                 newlyMarked, missingTrackCount - newlyMarked);
                }
            }

            if (m_progressCb)
                m_progressCb(1.0f, "Scan complete.");
            spdlog::info("Scan loop finished.");
            return true;
        }
    } // namespace database
} // namespace jucyaudio
