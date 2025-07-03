#include <Database/Includes/TrackInfo.h>
#include <Database/Scanners/AubioScanner.h>
#include <Database/Scanners/Id3TagScanner.h>
#include <Database/TrackScanner.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
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
            m_scanners.push_back(new scanners::Id3TagScanner{m_db.getTagManager()}); // Assuming getTagManager() returns a
                                                                                     // valid ITagManager reference
            // m_scanners.push_back(new scanners::AubioScanner{});
        }

        bool TrackScanner::scan(std::vector<FolderInfo> &foldersToScan, bool forceRescanAllFiles, ProgressCallback progressCb, CompletionCallback completionCb,
                                std::atomic<bool> *shouldCancel)
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
                    m_completionCb(false, "Scan loop failed to start or complete.");
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
            spdlog::info("Scan loop started. Force rescan: {}", m_forceRescanAll);

            // --- SETUP ---
            // Use unordered_map for efficient, non-ordered key-value storage.
            struct FolderScanStats
            {
                int numFiles{0};
                std::uintmax_t totalSizeBytes{0};
            };
            std::unordered_map<FolderId, FolderScanStats> folderStatsMap;

            int filesProcessedThisSession = 0;

            // Signal start with indeterminate progress. The UI should show a spinner/pulsing bar.
            if (m_progressCb)
                m_progressCb(-1, "Starting scan...");

            // --- STAGE 1: SINGLE-PASS SCAN AND PROCESS ---
            for (auto &folderInfo : foldersToScan)
            {
                if (m_pShouldCancel && *m_pShouldCancel)
                    return false;

                const juce::File scanDir(folderInfo.path.string());
                if (!scanDir.isDirectory())
                {
                    spdlog::warn("Scan folder does not exist or is not a directory: {}", pathToString(folderInfo.path));
                    continue;
                }

                spdlog::info("Scanning folder: {}", pathToString(folderInfo.path));
                if (m_progressCb)
                    m_progressCb(-1, std::format("Scanning: {} (currently at {:L} files)", folderInfo.path.stem().string(), filesProcessedThisSession));

                // Use JUCE's robust, recursive iterator with a wildcard filter.
                juce::RangedDirectoryIterator iter(scanDir,
                                                   true, // recursive
                                                   "*.mp3;*.wav;*.flac;*.ogg", juce::File::findFiles);

                for (const auto &entry : iter)
                {
                    if (m_pShouldCancel && *m_pShouldCancel)
                        return false;

                    const auto &file = entry.getFile();
                    filesProcessedThisSession++;
                    const auto currentParentDirectory = file.getParentDirectory();

                    // Update progress callback with running count to show activity, but not too frequently.
                    if (m_progressCb && (filesProcessedThisSession % 25 == 0))
                    {
                        auto relativePath = currentParentDirectory.getRelativePathFrom(scanDir);
                        m_progressCb(-1, std::format("Scanned {:L} files, currently in {}", filesProcessedThisSession, relativePath.toStdString()));
                    }

                    // Convert JUCE path back to std::filesystem::path for DB and model logic.
                    const std::filesystem::path filePath(file.getFullPathName().toStdString());
                    spdlog::debug("Processing: {}", pathToString(filePath));

                    // --- ALL YOUR PROVEN FILE-PROCESSING LOGIC, NOW FED BY JUCE::FILE ---

                    std::optional<TrackInfo> existingTrackOpt = m_db.getTrackByFilepath(filePath);
                    TrackInfo currentTrackInfo{};
                    bool needsFullAnalysis = true;

                    // Get file metadata robustly from the juce::File object.
                    const auto fsLastModified = Timestamp_t(std::chrono::system_clock::from_time_t(file.getLastModificationTime().toMilliseconds() / 1000));
                    const auto fsFileSize = static_cast<std::uintmax_t>(file.getSize());

                    // Update in-memory stats for the folder this file belongs to.
                    folderStatsMap[folderInfo.folderId].numFiles++;
                    folderStatsMap[folderInfo.folderId].totalSizeBytes += fsFileSize;

                    if (existingTrackOpt)
                    {
                        currentTrackInfo = *existingTrackOpt;

                        auto db_last_modified_seconds =
                            std::chrono::duration_cast<std::chrono::seconds>(currentTrackInfo.last_modified_fs.time_since_epoch()).count();
                        auto fs_last_modified_seconds = file.getLastModificationTime().toMilliseconds() / 1000;

                        if (!m_forceRescanAll && db_last_modified_seconds == fs_last_modified_seconds && currentTrackInfo.filesize_bytes == fsFileSize)
                        {
                            needsFullAnalysis = false;
                            spdlog::debug("Skipping full analysis for unchanged file: {}", pathToString(filePath));
                        }
                        else
                        {
                            spdlog::debug("File needs re-analysis. Path: {}", pathToString(filePath));
                        }

                        currentTrackInfo.last_modified_fs = fsLastModified;
                    }
                    else // This is a new track
                    {
                        currentTrackInfo.filepath = filePath;
                        currentTrackInfo.date_added = std::chrono::system_clock::now();
                        currentTrackInfo.last_modified_fs = fsLastModified;
                    }
                    currentTrackInfo.folderId = folderInfo.folderId;
                    currentTrackInfo.filesize_bytes = fsFileSize;
                    currentTrackInfo.is_missing = 0;

                    if (needsFullAnalysis)
                    {
                        for (const auto scanner : m_scanners)
                        {
                            scanner->processTrack(currentTrackInfo);
                        }
                    }
                    currentTrackInfo.last_scanned = std::chrono::system_clock::now();

                    DbResult saveResult = m_db.saveTrackInfo(currentTrackInfo);
                    if (!saveResult.isOk())
                    {
                        spdlog::error("Failed to save track info for {}: {}", pathToString(filePath), saveResult.errorMessage);
                    }
                }
            }

            // --- STAGE 2: FINALIZE AND UPDATE FOLDER INFO IN DATABASE ---
            spdlog::info("Finalizing scan and updating folder statistics...");
            if (m_progressCb)
                m_progressCb(99, "Finalizing..."); // Use 99% to show we're almost done

            for (auto &folderInfo : foldersToScan)
            {
                const auto it = folderStatsMap.find(folderInfo.folderId);
                if (it != folderStatsMap.end())
                {
                    folderInfo.numFiles = it->second.numFiles;
                    folderInfo.totalSizeBytes = it->second.totalSizeBytes;
                }
                else // This folder contained 0 valid audio files.
                {
                    folderInfo.numFiles = 0;
                    folderInfo.totalSizeBytes = 0;
                }
                folderInfo.lastScannedTime = std::chrono::system_clock::now();

                if (!m_db.getFolderDatabase().updateFolder(folderInfo))
                {
                    spdlog::error("Failed to update folder info for {}", pathToString(folderInfo.path));
                }
            }

            if (m_progressCb)
                m_progressCb(100, std::format("Scan complete. Processed {} files.", filesProcessedThisSession));

            spdlog::info("Scan loop finished. Processed {} files.", filesProcessedThisSession);
            return true;
        }
    } // namespace database
} // namespace jucyaudio
