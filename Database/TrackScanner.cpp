#include <Database/Includes/TrackInfo.h>
#include <Database/Scanners/AubioScanner.h>
#include <Database/Scanners/Id3TagScanner.h>
#include <Database/TrackScanner.h>
#include <Utils/AssortedUtils.h>
#include <set>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {

        TrackScanner::TrackScanner(ITrackDatabase &database)
            : m_db{database}
        {
            m_scanners.push_back(new scanners::Id3TagScanner{
                m_db.getTagManager()}); // Assuming getTagManager() returns a
                                        // valid ITagManager reference
            //m_scanners.push_back(new scanners::AubioScanner{});
        }

        bool TrackScanner::scan(
            std::vector<FolderInfo> &foldersToScan,
            bool forceRescanAllFiles, ProgressCallback progressCb,
            CompletionCallback completionCb, std::atomic<bool> *shouldCancel)
        {
            m_progressCb = progressCb;
            m_completionCb = completionCb;
            m_pShouldCancel = shouldCancel;
            m_forceRescanAll = forceRescanAllFiles;
            const auto success = scanLoop(foldersToScan);
            if (!success)
            {
                spdlog::error("Scan loop failed to start or complete.");
                if (m_completionCb)
                    m_completionCb(false,
                                   "Scan loop failed to start or complete.");
            }
            else if (m_completionCb)
            {
                m_completionCb(true, "Scan completed successfully.");
            }
            m_progressCb = nullptr;
            m_completionCb = nullptr;
            m_pShouldCancel = nullptr;
            return success;
        }

        bool TrackScanner::scanLoop(std::vector<FolderInfo> &foldersToScan)
        {
            spdlog::info("Scan loop started. Force rescan: {}",
                         m_forceRescanAll);

            // this loop first looks at the folders, then at all the files. This is a problem for us,
            // since we want to update the folder info, so we need to store both here.
            struct FileAndFolderInfo
            {
                std::filesystem::path filePath;
                FolderInfo* folderInfo; // Reference to the folder info for this file
            };

            std::vector<FileAndFolderInfo> filesToProcess;
            for (auto &folderInfo : foldersToScan)
            {
                const auto folder{
                    folderInfo.path}; // Use the path from FolderInfo directly
                if (m_pShouldCancel && *m_pShouldCancel)
                    return false;
                if (!std::filesystem::exists(folder) ||
                    !std::filesystem::is_directory(folder))
                {
                    spdlog::warn(
                        "Scan folder does not exist or is not a directory: {}",
                        pathToString(folder));
                    continue;
                }
                spdlog::info("Scanning folder: {}", pathToString(folder));
                folderInfo.lastScannedTime = std::chrono::system_clock::now();
                folderInfo.numFiles = 0; // Reset file count for this scan
                folderInfo.totalSizeBytes = 0; // Reset total size for this scan
                for (const auto &dir_entry :
                     std::filesystem::recursive_directory_iterator(
                         folder, std::filesystem::directory_options::
                                     skip_permission_denied))
                {
                    if (m_pShouldCancel && *m_pShouldCancel)
                        return false;

                    if (dir_entry.is_regular_file())
                    {
                        // Basic audio file extension check (can be improved)
                        std::string ext = dir_entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(),
                                       [](unsigned char c)
                                       {
                                           return static_cast<char>(
                                               std::tolower(c));
                                       });
                        if (ext == ".mp3" || ext == ".wav" || ext == ".flac" ||
                            ext == ".ogg" /* || ext == ".m4a" etc. */)
                        {
                            filesToProcess.push_back({dir_entry.path(), &folderInfo});
                        }
                    }
                }
            }
            const auto totalFilesToScanThisSession = filesToProcess.size();
            if (m_progressCb)
                m_progressCb(
                    0, std::format("Found {} files to process in {} folders.",
                                   totalFilesToScanThisSession,
                                   foldersToScan.size()));

            int filesProcessedThisSession = 0;
            for (const auto &fileAndFolderInfo : filesToProcess)
            {
                if (m_pShouldCancel && *m_pShouldCancel)
                    return false;

                const auto &filePath = fileAndFolderInfo.filePath;
                const auto currentFileProcessing = filePath.filename();
                double overallProgressEstimate = 0.0;
                if (totalFilesToScanThisSession > 0)
                {
                    overallProgressEstimate =
                        static_cast<double>(filesProcessedThisSession) /
                        totalFilesToScanThisSession;
                    if (m_progressCb)
                        m_progressCb(
                            static_cast<int>(overallProgressEstimate * 100.0),
                            std::format("Scanning {}", pathToString(currentFileProcessing)));
                }

                spdlog::debug("Processing: {}", pathToString(filePath));

                std::optional<TrackInfo> existingTrackOpt =
                    m_db.getTrackByFilepath(filePath);
                TrackInfo currentTrackInfo{};
                bool needsFullAnalysis = true;

                Timestamp_t fsLastModified{};
                std::uintmax_t fsFileSize = 0;
                try
                {
                    if (std::filesystem::exists(filePath))
                    { // Re-check existence, could have been deleted
                        const auto ftime =
                            std::filesystem::last_write_time(filePath);
                        fsLastModified =
                            Timestamp_t(std::chrono::duration_cast<
                                        std::chrono::system_clock::duration>(
                                ftime.time_since_epoch()));
                        fsFileSize = std::filesystem::file_size(filePath);
                    }
                    else
                    {
                        spdlog::warn("File disappeared during scan: {}",
                                     pathToString(filePath));
                        if (existingTrackOpt && existingTrackOpt->trackId != -1)
                        {
                            m_db.setTrackPathMissing(existingTrackOpt->trackId,
                                                     true);
                        }
                        filesProcessedThisSession++;
                        continue;
                    }
                }
                catch (const std::filesystem::filesystem_error &e)
                {
                    spdlog::error("Filesystem error accessing {}: {}",
                                  pathToString(filePath), e.what());
                    filesProcessedThisSession++;
                    continue;
                }
                auto& folderInfo = *fileAndFolderInfo.folderInfo;
                ++(folderInfo.numFiles);
                folderInfo.totalSizeBytes += fsFileSize;
                if (!m_db.getFolderDatabase().updateFolder(folderInfo))
                {
                    spdlog::error("Failed to update folder info for {}",
                                  pathToString(folderInfo.path));
                }

                if (existingTrackOpt)
                {
                    currentTrackInfo = *existingTrackOpt;
                    // Convert both to seconds since epoch for comparison
                    auto db_last_modified_seconds =
                        std::chrono::duration_cast<std::chrono::seconds>(
                            currentTrackInfo.last_modified_fs
                                .time_since_epoch())
                            .count();
                    auto fs_last_modified_seconds =
                        std::chrono::duration_cast<std::chrono::seconds>(
                            fsLastModified.time_since_epoch())
                            .count();

                    if (!m_forceRescanAll &&
                        db_last_modified_seconds ==
                            fs_last_modified_seconds && // Compare at second
                                                        // precision
                        currentTrackInfo.filesize_bytes == fsFileSize)
                    {
                        needsFullAnalysis = false;
                        spdlog::debug("Skipping full analysis for unchanged "
                                      "file (time/size match): {}",
                                      pathToString(filePath));
                    }
                    else
                    {
                        spdlog::info(
                            "File needs analysis. Force: {}. DB Time: {}, FS "
                            "Time: {}. DB Size: {}, FS Size: {}",
                            m_forceRescanAll, db_last_modified_seconds,
                            fs_last_modified_seconds,
                            currentTrackInfo.filesize_bytes, fsFileSize);
                    }
                    // Always update current filesystem info if track exists in
                    // DB
                    currentTrackInfo.last_modified_fs = fsLastModified;
                    currentTrackInfo.filesize_bytes = fsFileSize;
                    currentTrackInfo.is_missing =
                        0; // Mark as not missing if found
                }
                else
                {
                    // New track
                    currentTrackInfo.filepath = filePath;
                    currentTrackInfo.date_added =
                        std::chrono::system_clock::now();
                    currentTrackInfo.last_modified_fs = fsLastModified;
                    currentTrackInfo.filesize_bytes = fsFileSize;
                }

                if (needsFullAnalysis)
                {
                    for (const auto scanner : m_scanners)
                    {
                        scanner->processTrack(currentTrackInfo);
                    }
                }
                currentTrackInfo.last_scanned =
                    std::chrono::system_clock::now();

                DbResult saveResult = m_db.saveTrackInfo(
                    currentTrackInfo); // This will INSERT or UPDATE
                if (!saveResult.isOk())
                {
                    spdlog::error("Failed to save track info for {}: {}",
                                  pathToString(filePath),
                                  saveResult.errorMessage);
                }
                filesProcessedThisSession++;
            }

            if (m_progressCb)
                m_progressCb(
                    100, std::format("Scan loop finished. Processed {} files",
                                     filesProcessedThisSession));

            spdlog::info("Scan loop finished. Processed {} files.",
                         filesProcessedThisSession);
            return true;
        }
    } // namespace database
} // namespace jucyaudio
