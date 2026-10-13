#include <Database/BackgroundService.h>
#include <Database/BackgroundTasks/AudioAnalysis.h>
#include <Database/BackgroundTasks/BpmAnalysisTask.h>
#include <Database/BackgroundTasks/Mp3QuickCheck.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>
#include <algorithm>
#include <condition_variable>
#include <format>
#include <functional>
#include <juce_audio_formats/juce_audio_formats.h>
#include <mutex>
#include <queue>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        namespace background_tasks
        {
            std::string_view describe(SkipReason reason)
            {
                switch (reason)
                {
                case SkipReason::FileMissing:
                    return "file missing";
                case SkipReason::Mp3PreCheck:
                    return "skipped by the MP3 size/bitrate rule";
                case SkipReason::NoDecoder:
                    return "no decoder could open it";
                case SkipReason::ReadError:
                    return "reading it failed";
                }
                return "unknown";
            }

            bool isRemovable(SkipReason reason)
            {
                return reason != SkipReason::Mp3PreCheck;
            }

            std::vector<TrackId> removableTrackIds(const std::vector<SkippedTrack> &skipped)
            {
                std::vector<TrackId> ids;
                for (const auto &entry : skipped)
                {
                    if (isRemovable(entry.reason))
                    {
                        ids.push_back(entry.track.trackId);
                    }
                }
                return ids;
            }

            std::string summarizeSkippedTracks(const std::vector<SkippedTrack> &skipped)
            {
                std::string text = std::format("{} skipped", skipped.size());
                const char *separator = ": ";
                for (const auto reason : {SkipReason::FileMissing, SkipReason::Mp3PreCheck, SkipReason::NoDecoder, SkipReason::ReadError})
                {
                    const auto count = std::ranges::count_if(skipped, [reason](const SkippedTrack &entry) { return entry.reason == reason; });
                    if (count > 0)
                    {
                        text += std::format("{}{} {}", separator, count, describe(reason));
                        separator = ", ";
                    }
                }
                return text + ".";
            }

            std::optional<size_t> applySkippedTracksChoice(IWorkingSetManager &workingSets,
                WorkingSetId workingSetId,
                const std::vector<SkippedTrack> &skipped,
                bool confirmed)
            {
                const auto removable = removableTrackIds(skipped);
                if (!confirmed || workingSetId <= 0 || removable.empty())
                {
                    return size_t{0};
                }
                if (!workingSets.removeTracksFromWorkingSet(workingSetId, removable))
                {
                    spdlog::error("Could not remove {} unreadable track(s) from working set {}", removable.size(), workingSetId);
                    return std::nullopt;
                }
                spdlog::info("Removed {} unreadable track(s) from working set {}", removable.size(), workingSetId);
                return removable.size();
            }

            TrackAnalysis analyzeTrack(juce::AudioFormatManager &formats, const std::filesystem::path &trackPath)
            {
                const auto skipped = [](SkipReason reason, std::string detail = {})
                {
                    return TrackAnalysis{std::nullopt, reason, std::move(detail)};
                };

                try
                {
                    juce::File audioFile{ui::jucePathFromFs(trackPath)};
                    if (!audioFile.existsAsFile())
                    {
                        spdlog::debug("File does not exist: {}", trackPath.string());
                        return skipped(SkipReason::FileMissing);
                    }

                    const auto startTime = std::chrono::steady_clock::now();
                    std::unique_ptr<juce::AudioFormatReader> reader{formats.createReaderFor(audioFile)};
                    const auto openTime = std::chrono::steady_clock::now() - startTime;
                    if (std::chrono::duration_cast<std::chrono::seconds>(openTime).count() > 1)
                    {
                        spdlog::warn("File took {} ms to open: {}", std::chrono::duration_cast<std::chrono::milliseconds>(openTime).count(), trackPath.string());
                    }
                    if (!reader)
                    {
                        spdlog::debug("Cannot create reader for: {}", trackPath.string());
                        return skipped(SkipReason::NoDecoder);
                    }

                    // The middle minute of the track, or all of it if it is shorter.
                    const double totalDurationSeconds = reader->lengthInSamples / reader->sampleRate;
                    const double analysisDurationSeconds = 60.0;
                    int64_t startSample = 0;
                    int numSamplesToRead = static_cast<int>(reader->lengthInSamples);
                    if (totalDurationSeconds > analysisDurationSeconds)
                    {
                        startSample = static_cast<int64_t>(((totalDurationSeconds / 2.0) - (analysisDurationSeconds / 2.0)) * reader->sampleRate);
                        numSamplesToRead = static_cast<int>(analysisDurationSeconds * reader->sampleRate);
                        if (startSample + numSamplesToRead > reader->lengthInSamples)
                        {
                            numSamplesToRead = static_cast<int>(reader->lengthInSamples - startSample);
                        }
                    }

                    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), numSamplesToRead);
                    // A reader can open a file and still fail to decode it. Analyzing the buffer anyway
                    // would store a BPM computed from whatever it holds and mark the track ok.
                    if (!reader->read(&buffer, 0, numSamplesToRead, startSample, true, true))
                    {
                        spdlog::debug("Decoding failed after opening: {}", trackPath.string());
                        return skipped(SkipReason::ReadError, "decoding failed after the file was opened");
                    }

                    return TrackAnalysis{analyzeAudioBuffer(buffer, reader->sampleRate), SkipReason::ReadError, {}};
                }
                catch (const std::exception &e)
                {
                    spdlog::debug("Exception reading {}: {}", trackPath.string(), e.what());
                    return skipped(SkipReason::ReadError, e.what());
                }
                catch (...)
                {
                    spdlog::error("Unknown non-std::exception reading {}", trackPath.string());
                    return skipped(SkipReason::ReadError, "unknown exception");
                }
            }

            BpmAnalysisTask::BpmAnalysisTask(std::vector<TrackInfo> trackInfos)
                : ILongRunningTask{"Running BPM Analysis", true},
                  m_trackInfos{std::move(trackInfos)}
            {
            }

            void BpmAnalysisTask::run(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel)
            {
                if (!theBackgroundTaskService.pause())
                {
                    spdlog::warn("BpmAnalysisTask: Background service did not pause in time, proceeding anyway");
                }
                try
                {
                    runInternal(progressCb, completionCb, shouldCancel);
                }
                catch (const std::exception &e)
                {
                    spdlog::error("BpmAnalysisTask: Exception during analysis: {}", e.what());
                    completionCb(false, std::format("Task failed: {}", e.what()));
                }
                theBackgroundTaskService.resume();
            }

            inline bool shouldProcess(const TrackInfo& trackInfo)
            {
                return !trackInfo.bpm.has_value() || trackInfo.bpm.value() <= 0;
            }

            void BpmAnalysisTask::runInternal(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel)
            {
                // 1. Filter tracks to find those that actually need analysis.
                spdlog::info("BPM Analysis Task: Pre-reading track data and filtering tracks that need analysis...");
                progressCb(-1, "Querying tracks for analysis...");

                std::vector<const TrackInfo *> tracksToProcess;
                for (const auto& track : m_trackInfos)
                {
                    if (shouldProcess(track))
                    {
                        tracksToProcess.push_back(&track);
                    }
                }

                if (shouldCancel)
                {
                    completionCb(false, "Task cancelled during track filtering.");
                    return;
                }
                const auto nrTracksToProcess{tracksToProcess.size()};
                if (nrTracksToProcess == 0)
                {
                    completionCb(true, "All selected tracks have already been analyzed.");
                    return;
                }

                // --- Configuration ---
                const unsigned int numCores = std::thread::hardware_concurrency();
                const unsigned int numWorkerThreads = std::min(8u, std::max(4u, numCores));
                const size_t batchSize = 100;

                spdlog::info("Starting BPM analysis for {} tracks using {} worker threads.",
                    ui::formatStandardStringNumber(nrTracksToProcess),
                    ui::formatStandardStringNumber(numWorkerThreads));

                // --- Data Structures ---
                struct AnalysisResult
                {
                    TrackId trackId;
                    AudioMetadata metadata;
                };

                std::queue<AnalysisResult> resultsQueue;
                std::mutex resultsMutex;

                std::atomic<size_t> nextTrackIndex = 0;
                std::atomic<size_t> tracksProcessed = 0;
                std::atomic<size_t> tracksAnalyzed = 0;
                std::atomic<size_t> tracksSkipped = 0;

                // Thread-safe collection of skipped tracks. A track that could not be read is marked
                // BadFormat, which the background analyzer then leaves alone; one skipped by the MP3
                // size/bitrate rule is not, because nothing is wrong with the file (#77).
                std::vector<SkippedTrack> skippedTracks;
                std::mutex skippedMutex;
                const auto skip = [&](const TrackInfo &trackInfo, SkipReason reason, std::string detail = {})
                {
                    if (reason != SkipReason::Mp3PreCheck)
                    {
                        theTrackLibrary.getTrackDatabase().updateTrackStatus(trackInfo.trackId, TrackStatus::BadFormat);
                    }
                    {
                        std::lock_guard<std::mutex> lock(skippedMutex);
                        skippedTracks.push_back(SkippedTrack{trackInfo, reason, std::move(detail)});
                    }
                    tracksSkipped++;
                };

                // Worker threads pool
                std::vector<std::thread> workers;

                // --- Worker Threads ---
                for (size_t i = 0; i < numWorkerThreads; ++i)
                {
                    workers.emplace_back(
                        [&]
                        {
                            juce::AudioFormatManager formatManager;
                            formatManager.registerBasicFormats();

                            while (!shouldCancel)
                            {
                                // Get next track to process
                                size_t trackIndex = nextTrackIndex.fetch_add(1);
                                if (trackIndex >= tracksToProcess.size())
                                {
                                    break; // No more tracks
                                }

                                const auto trackInfo = tracksToProcess[trackIndex];
                                // analyzeTrack never throws; resolving the path can, and an exception
                                // leaving a worker thread would terminate the application.
                                TrackAnalysis outcome;
                                try
                                {
                                    const auto trackPath = trackInfo->reconstructFullPath();
                                    const auto extension = trackPath.extension().string();
                                    if ((extension == ".mp3" || extension == ".MP3") && !Mp3QuickCheck::checkMp3File(trackPath).has_value())
                                    {
                                        // A policy skip: larger than 50 MB, below 64 kbps, or a VBR file over 30 MB.
                                        // Here rather than in analyzeTrack, which the background analyzer shares
                                        // and which has never applied it there.
                                        outcome = TrackAnalysis{std::nullopt, SkipReason::Mp3PreCheck, {}};
                                    }
                                    else
                                    {
                                        outcome = analyzeTrack(formatManager, trackPath);
                                    }
                                }
                                catch (const std::exception &e)
                                {
                                    outcome = TrackAnalysis{std::nullopt, SkipReason::ReadError, e.what()};
                                }
                                catch (...)
                                {
                                    outcome = TrackAnalysis{std::nullopt, SkipReason::ReadError, "unknown exception"};
                                }

                                // A cancel that arrived while this track was being read: drop the result.
                                if (shouldCancel)
                                    break;

                                if (outcome.metadata.has_value())
                                {
                                    {
                                        std::lock_guard<std::mutex> lock(resultsMutex);
                                        resultsQueue.push({trackInfo->trackId, *outcome.metadata});
                                    }
                                    tracksAnalyzed++;
                                }
                                else
                                {
                                    skip(*trackInfo, outcome.reason, std::move(outcome.detail));
                                }

                                tracksProcessed++;
                            }

                            spdlog::debug("Worker thread exiting (cancel={}, processed={})", shouldCancel.load(), tracksProcessed.load());
                        });
                }

                // --- Main thread: collect results and commit in batches ---
                size_t tracksWritten = 0;
                std::vector<std::pair<TrackId, AudioMetadata>> resultsBatch;
                resultsBatch.reserve(batchSize);

                auto lastCommitTime = std::chrono::steady_clock::now();

                while (!shouldCancel) // -V1044 this loop can be exited via external break signal
                {
                    // Collect results from queue
                    {
                        std::lock_guard<std::mutex> lock{resultsMutex};
                        while (!resultsQueue.empty() && resultsBatch.size() < batchSize)
                        {
                            resultsBatch.emplace_back(resultsQueue.front().trackId, resultsQueue.front().metadata);
                            resultsQueue.pop();
                        }
                    }

                    // Determine if we should commit
                    auto now = std::chrono::steady_clock::now();
                    auto timeSinceLastCommit = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCommitTime);

                    bool shouldCommit = false;
                    bool allDone = (tracksProcessed >= nrTracksToProcess);

                    if (!resultsBatch.empty())
                    {
                        if (resultsBatch.size() >= batchSize)
                        {
                            // Full batch ready
                            shouldCommit = true;
                            spdlog::debug("Committing full batch of {} tracks", resultsBatch.size());
                        }
                        else if (allDone)
                        {
                            // All tracks done, commit remaining
                            shouldCommit = true;
                            spdlog::debug("Committing final batch of {} tracks", resultsBatch.size());
                        }
                        else if (timeSinceLastCommit.count() > 5000)
                        {
                            // It's been 5 seconds, commit what we have for progress
                            shouldCommit = true;
                            spdlog::debug("Committing partial batch of {} tracks after timeout", resultsBatch.size());
                        }
                    }

                    if (shouldCommit && !resultsBatch.empty())
                    {
                        theTrackLibrary.getTrackDatabase().updateTrackBpm(resultsBatch);
                        tracksWritten += resultsBatch.size();
                        resultsBatch.clear();
                        lastCommitTime = now;
                    }

                    // Update progress periodically
                    auto timeSinceLastProgress = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCommitTime);
                    if (timeSinceLastProgress.count() > 500)
                    { // Every 500ms
                        size_t processed = tracksProcessed.load();
                        int progressPercent = static_cast<int>((static_cast<float>(processed) / nrTracksToProcess) * 100.0f);
                        const auto status = std::format("Processed {} / {} tracks ({} analyzed, {} skipped)...",
                            ui::formatStandardStringNumber(processed),
                            ui::formatStandardStringNumber(nrTracksToProcess),
                            ui::formatStandardStringNumber(tracksWritten),
                            ui::formatStandardStringNumber(tracksSkipped.load()));
                        progressCb(progressPercent, status);
                        lastCommitTime = now;
                    }

                    // Check if we're done
                    if (allDone && tracksWritten >= tracksAnalyzed)
                    {
                        spdlog::info("All tracks processed. Processed: {}, Written: {}, Analyzed: {}, Skipped: {}",
                            tracksProcessed.load(),
                            tracksWritten,
                            tracksAnalyzed.load(),
                            tracksSkipped.load());
                        break;
                    }

                    // Small sleep to avoid busy waiting
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }

                // --- Cleanup ---
                if (shouldCancel)
                {
                    spdlog::info("BPM Analysis: Cancel detected, waiting for worker threads");
                }

                // Always join all threads - never detach, as detached threads would access
                // freed data (tracksToProcess, resultsQueue, atomics, etc.)
                for (auto &worker : workers)
                {
                    if (worker.joinable())
                    {
                        worker.join();
                    }
                }

                // Commit any remaining results
                if (!resultsBatch.empty() && !shouldCancel)
                {
                    spdlog::debug("Committing final {} results", resultsBatch.size());
                    theTrackLibrary.getTrackDatabase().updateTrackBpm(resultsBatch);
                    tracksWritten += resultsBatch.size();
                }

                // The workers are joined, so this is the only thread left touching the list.
                m_skippedTracks = std::move(skippedTracks);

                // Final status
                if (shouldCancel)
                {
                    std::string finalStatus = std::format("Cancelled. Analyzed {} / {} tracks.",
                        ui::formatStandardStringNumber(tracksWritten),
                        ui::formatStandardStringNumber(nrTracksToProcess));
                    if (!m_skippedTracks.empty())
                    {
                        finalStatus += " " + summarizeSkippedTracks(m_skippedTracks);
                    }
                    spdlog::info("BPM Analysis cancelled: {} analyzed, {} skipped", tracksWritten, m_skippedTracks.size());
                    completionCb(false, finalStatus);
                }
                else
                {
                    std::string finalStatus = std::format("Successfully analyzed {} / {} tracks.",
                        ui::formatStandardStringNumber(tracksWritten),
                        ui::formatStandardStringNumber(nrTracksToProcess));
                    if (!m_skippedTracks.empty())
                    {
                        finalStatus += " " + summarizeSkippedTracks(m_skippedTracks);
                    }
                    spdlog::info("BPM Analysis complete: {} analyzed, {} skipped", tracksWritten, m_skippedTracks.size());
                    progressCb(100, finalStatus);
                    completionCb(true, finalStatus);
                }
            }
        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio