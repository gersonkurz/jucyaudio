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

            // Every track in scope by (normalized filename, size), for recognising a file that moved.
            //
            // A file dragged from one folder to another is a new file in one place and a vanished one in
            // the other, and the scanner used to treat it as exactly that: insert a fresh row, mark the
            // old one missing. The new row has a new track id, so every mix, working set and album entry
            // stays pointing at the row that is now missing - the file is right there on disk and the
            // mixes that use it are broken anyway.
            //
            // Name and size together, because the name alone matches every copy of a track and the size
            // alone matches nothing in particular. Neither is proof, so a name and size that more than
            // one track in scope answers to is treated as no match at all - see below.
            std::unordered_map<std::string, std::vector<TrackId>> byNameAndSize;

            /// @brief Real filenames by track id, for confirming where a candidate used to be.
            std::unordered_map<TrackId, std::string> filenameById;

            // Folders that a root actually reached this run.
            //
            // A track row missing from the walk means its file is gone only if somebody actually looked.
            // A root whose path cannot be reconstructed is skipped outright, and a root on a drive that
            // is not plugged in cannot be walked at all - and every row underneath then sits in the
            // leftovers looking exactly like a deleted file. Reassigning identity on that evidence hands
            // a track, and every mix that uses it, to a copy elsewhere while the original is still there
            // on a disconnected disk.
            //
            // So eligibility is recorded positively: a folder gets in here only when the root covering it
            // was resolved and found on disk. Individual folders inside it may well have been deleted -
            // that is what moving a folder looks like - and that is the case this is meant to allow.
            std::unordered_set<FolderId> inspectedFolders;

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
                byNameAndSize[std::format("{}|{}", key, track.filesize_bytes)].push_back(track.trackId);

                // The name as it is actually spelled on disk, which the cache key is not. Needed to go
                // back and look at where a candidate used to be before believing it moved.
                filenameById[track.trackId] = track.filename;
                //++index;
            }

            spdlog::debug("Cache built with {} folders and {} tracks.", existingTrackCache.size(), tracksInScope.size());

            // Files already handled in THIS scan run, keyed by (folderId, normalized filename).
            // Overlapping roots (e.g. D:\MP3 and D:\MP3\DARKGAZE) reach the same physical folder
            // twice; without this guard the destructive existingTrackCache erase makes the second
            // encounter look new and insert a duplicate. The UNIQUE(folder_id, filename) index is
            // the hard backstop; this just avoids the wasted insert + failure log.
            std::set<std::pair<FolderId, std::string>> processedThisRun;

            // Files that look new but match a track in scope by name and size, held back until the walk
            // is over.
            //
            // Deciding a move on the spot would be wrong, because "the old file is gone" is not known
            // until every folder has been visited. Re-identify eagerly and a file that had merely been
            // copied - the original still sitting where it always was - moves the row to the copy, and
            // the original is then inserted as a new track. The row and the file swap places, and every
            // mix that used the original now plays the copy.
            //
            // So the walk ends first, the leftovers say which files are genuinely gone, and only then is
            // a match acted on. Only files with a candidate wait; everything else is inserted as it is
            // found, which is the whole of a first scan.
            struct PossibleMove final
            {
                TrackInfo info;                        ///< The file as scanned, with no track id yet.
                std::string groupKey;                  ///< Its (normalized name, size) group in byNameAndSize.
                ScannedFields read{ScannedFields::None}; ///< What the scanners actually established.
            };
            std::vector<PossibleMove> possiblyMoved;

            // Writes that did not happen, for any reason.
            //
            // These used to be logged and stepped over, and the scan then said it had succeeded. That is
            // how a forced rescan could collide with the unique index on every single file it was asked
            // to refresh and still report success - the one thing that would have made the bug visible
            // was the report, and the report was not looking.
            int writeFailures = 0;

            int filesProcessedThisSession = 0;
            for (const auto &rootFolderId : folderIdsToScan)
            {
                if (m_pShouldCancel && *m_pShouldCancel)
                    return false;

                auto rootFolderPath = m_db.reconstructFullPath(rootFolderId);
                if (rootFolderPath.empty())
                {
                    spdlog::warn("Could not work out where folder {} is; skipping it.", rootFolderId);
                    continue;
                }

                // Present and readable as a directory, before anything under it counts as inspected.
                // An unplugged drive answers the same way a deleted folder does, and the difference
                // matters to every row underneath.
                std::error_code rootEc;
                if (!std::filesystem::is_directory(rootFolderPath, rootEc))
                {
                    spdlog::warn("Root {} is not there or cannot be read; skipping it, and nothing under it counts as looked at.",
                        pathToString(rootFolderPath));
                    continue;
                }

                for (const auto folderInRoot : folderDatabase.getAllChildFolders({rootFolderId}))
                {
                    inspectedFolders.insert(folderInRoot);
                }

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

                    // The row this file already has, if it has one. -1 means it looked new here.
                    TrackId existingTrackId{-1};

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
                            existingTrackId = known->second;
                            item->second.erase(known);

                            // The file is back. Clear the flag here, before the early-out below: the
                            // ordinary scan is the un-forced one, so clearing after that continue would
                            // mean it effectively never ran. Nothing else about the track is touched -
                            // the row keeps its id, and with it every mix reference and working set
                            // membership that points at it.
                            if (previouslyMissing.contains(existingTrackId))
                            {
                                const auto result{m_db.setTrackPathMissing(existingTrackId, false)};
                                if (!result.isOk())
                                {
                                    spdlog::error("Failed to clear missing flag for track {} ({}): {}", existingTrackId, filename, result.errorMessage);
                                    return false;
                                }
                                spdlog::info("Track {} ({}) is back on disk; cleared its missing flag.", existingTrackId, filename);
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
                    currentTrackInfo.trackId = existingTrackId;
                    currentTrackInfo.filename = filename;
                    currentTrackInfo.folderId = parentFolderId;
                    currentTrackInfo.date_added = std::chrono::system_clock::now();
                    currentTrackInfo.last_modified_fs = timestampFromInt64(file.getLastModificationTime().toMilliseconds());
                    currentTrackInfo.filesize_bytes = file.getSize();
                    assert(currentTrackInfo.is_missing == false);
                    currentTrackInfo.last_scanned = std::chrono::system_clock::now();

                    // What the scanners actually established, field group by field group.
                    //
                    // Not a yes-or-no: a file can give up its tags and defeat the audio property reader,
                    // and the duration, samplerate, channels and bitrate are then zeroes that say
                    // nothing about the file. Written over an existing row, those zeroes are not a scan
                    // result - they are the erasure of one - so the write below is told exactly which
                    // fields to believe.
                    auto readFields = ScannedFields::None;
                    for (const auto &scanner : m_scanners)
                    {
                        readFields |= scanner->processTrack(currentTrackInfo, fullPath);
                    }

                    if (readFields == ScannedFields::None)
                    {
                        spdlog::warn("Nothing could be read from {}; its stored metadata is left as it is.", pathToString(fullPath));
                    }

                    if (existingTrackId > 0)
                    {
                        // A forced rescan of a row that is already there. saveTrackInfo would take the
                        // INSERT branch, because it decides on the track id and this TrackInfo was built
                        // from the file rather than read from the database - and the insert then collides
                        // with UNIQUE(folder_id, filename), which is how a forced rescan came to fail on
                        // every file it was supposed to refresh. Setting the id instead is not enough
                        // either: that UPDATE writes every column, and the columns a file cannot answer
                        // for would go back to defaults.
                        const auto result{m_db.updateScannedTrackData(currentTrackInfo, readFields)};
                        if (!result.isOk())
                        {
                            spdlog::error("Failed to refresh track {} ({}): {}", existingTrackId, filename, result.errorMessage);
                            ++writeFailures;
                        }
                        continue;
                    }

                    // Nothing under this name in this folder. Before inserting: is this a file that moved
                    // here from somewhere else in scope? Decided after the walk - see possiblyMoved.
                    auto groupKey{std::format("{}|{}", normalizedFilename, currentTrackInfo.filesize_bytes)};
                    if (byNameAndSize.contains(groupKey))
                    {
                        // The key is carried along rather than rebuilt later. Two places composing the
                        // same key is two places to get it wrong.
                        possiblyMoved.push_back({std::move(currentTrackInfo), std::move(groupKey), readFields});
                        continue;
                    }

                    if (!m_db.saveTrackInfo(currentTrackInfo).isOk())
                    {
                        spdlog::error("Failed to save track info for {}", filename);
                        ++writeFailures;
                    }
                }
            }

            // The walk is over, so the leftovers in existingTrackCache are exactly the tracks whose files
            // were not found. A held-back file matching one of those is a file that moved.
            if (!possiblyMoved.empty())
            {
                // The leftovers by id, so a match can be recognised and then removed from the cache - a
                // track that has been re-identified must not also be marked missing or deleted.
                std::unordered_map<TrackId, std::pair<FolderId, std::string>> leftovers;
                for (const auto &folderEntry : existingTrackCache)
                {
                    for (const auto &track : folderEntry.second)
                    {
                        leftovers[track.second] = {folderEntry.first, track.first};
                    }
                }

                // Is the file this row names really not there any more?
                //
                // Two things, and the first is the one that is easy to leave out: somebody has to have
                // looked. A row is only eligible if the root covering its folder was resolved and found
                // on disk this run - see inspectedFolders. Then, and only then, the file's own absence
                // means what it looks like.
                //
                // Note it is the file that is checked, not its folder. Moving a whole folder somewhere
                // else leaves the old folder deleted, and that is the ordinary case this exists to
                // support - so a missing folder is fine, and a missing root is not.
                const auto sourceConfirmedGone = [this, &filenameById, &leftovers, &inspectedFolders](TrackId candidate) -> bool
                {
                    const auto name{filenameById.find(candidate)};
                    const auto where{leftovers.find(candidate)};
                    if (name == filenameById.end() || where == leftovers.end())
                    {
                        return false;
                    }

                    if (!inspectedFolders.contains(where->second.first))
                    {
                        return false;
                    }

                    const auto folder{m_db.reconstructFullPath(where->second.first)};
                    if (folder.empty())
                    {
                        return false;
                    }

                    // Decided on the status type, not on the error code. An absent file is reported
                    // through both on this platform - not_found in the status and an errno in the code -
                    // so treating any error as "cannot tell" would make every genuinely moved file
                    // unrecognisable, while treating a clear code as proof would do the opposite. A
                    // folder that cannot be read at all answers with neither, and is refused.
                    std::error_code fileEc;
                    const auto fileStatus = std::filesystem::symlink_status(folder / pathFromString(name->second), fileEc);
                    return fileStatus.type() == std::filesystem::file_type::not_found;
                };

                // How many newly found files fell into each group.
                std::unordered_map<std::string, int> newFilesPerGroup;
                for (const auto &moved : possiblyMoved)
                {
                    ++newFilesPerGroup[moved.groupKey];
                }

                for (auto &moved : possiblyMoved)
                {
                    auto &trackInfo{moved.info};
                    const auto &candidates{byNameAndSize[moved.groupKey]};
                    const auto newFiles{newFilesPerGroup[moved.groupKey]};

                    // One file, one row, that row's file confirmed gone. All three, or it is not a move.
                    //
                    //  - One row in the group. Not "one gone row": a second row of the same name and
                    //    size that is still on disk means this name and size does not identify anything,
                    //    and the fact that one of them vanished does not make the other one's twin this
                    //    file.
                    //  - One new file in the group. Two identical new files competing for one vanished
                    //    row would otherwise hand the identity to whichever the directory walk returned
                    //    first, which is not a decision, it is an accident.
                    //  - The old file is confirmed absent, by going and looking. Being a leftover is not
                    //    that: a root whose path could not be reconstructed is skipped outright, and the
                    //    directory walk reports nothing about a subtree it could not enter, so every row
                    //    under an unreadable or disconnected folder lands in the leftovers looking
                    //    exactly like a file that was deleted. Acting on that reassigns a track, and
                    //    every mix that uses it, to a copy somewhere else while the original is still
                    //    sitting on a drive that happened to be unplugged.
                    //
                    // Anything else is inserted as a new track. That loses the history, which can be put
                    // back by hand; the alternative attaches a mix to the wrong file and never says so.
                    const bool oneMatch{candidates.size() == 1 && newFiles == 1 && leftovers.contains(candidates.front())};
                    const bool confirmedGone{oneMatch && sourceConfirmedGone(candidates.front())};
                    if (!confirmedGone)
                    {
                        spdlog::info("{} is not unambiguously a moved file ({} matching row(s), {} matching new file(s), old file confirmed gone: {}); "
                                     "inserting it as new.",
                            trackInfo.filename,
                            candidates.size(),
                            newFiles,
                            confirmedGone);

                        if (!m_db.saveTrackInfo(trackInfo).isOk())
                        {
                            spdlog::error("Failed to save track info for {}", trackInfo.filename);
                            ++writeFailures;
                        }
                        continue;
                    }

                    trackInfo.trackId = candidates.front();
                    const auto result{m_db.updateScannedTrackData(trackInfo, moved.read)};
                    if (!result.isOk())
                    {
                        // Reported, then inserted anyway: a file on disk that the library does not list
                        // is the worse outcome of the two. Counted either way - the move did not happen,
                        // and the mixes that used the old row are still pointing at a missing track.
                        spdlog::error("Could not move track {} to its new folder ({}): {}", trackInfo.trackId, trackInfo.filename, result.errorMessage);
                        ++writeFailures;
                        trackInfo.trackId = -1;
                        if (!m_db.saveTrackInfo(trackInfo).isOk())
                        {
                            spdlog::error("Failed to save track info for {}", trackInfo.filename);
                        }
                        continue;
                    }

                    spdlog::info("Track {} ({}) moved; kept its id, and every mix that uses it.", trackInfo.trackId, trackInfo.filename);

                    // Out of the cache, or the loop below would mark a track missing whose file is on
                    // disk under a name this scan has just written down. The flag itself is already
                    // clear: updateScannedTrackData wrote is_missing = 0.
                    //
                    // Nothing is taken out of the leftovers map, because nothing else can reach this
                    // track: a track belongs to one group, and a group that gets here has exactly one
                    // file in it.
                    const auto &where{leftovers[trackInfo.trackId]};
                    existingTrackCache[where.first].erase(where.second);
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
                        // Counted, like every other write that did not happen. The user asked for these
                        // rows to be gone; a scan that leaves them and says it succeeded is the same
                        // failure as the forced rescan that wrote nothing and said it succeeded.
                        spdlog::error("Failed to remove missing tracks from the database: {}", removeTrackSuccess.errorMessage);
                        ++writeFailures;
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
                        ++writeFailures;
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

            if (writeFailures > 0)
            {
                spdlog::error("Scan loop finished, but {} write(s) failed. See the errors above.", writeFailures);
                return false;
            }

            spdlog::info("Scan loop finished.");
            return true;
        }
    } // namespace database
} // namespace jucyaudio
