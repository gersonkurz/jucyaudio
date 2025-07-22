#include <Database/BackgroundTasks/BpmAnalysisTask.h>
#include <Database/BackgroundTasks/AudioAnalysis.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <juce_audio_formats/juce_audio_formats.h>
#include <Database/BackgroundService.h>
#include <Utils/UiUtils.h>

namespace jucyaudio
{
    namespace database
    {
        namespace background_tasks
        {
            // A simple thread pool implementation for this task
            class ThreadPool
            {
            public:
                ThreadPool(size_t numThreads)
                    : m_stop{false}
                {
                    for (size_t i = 0; i < numThreads; ++i)
                    {
                        m_workers.emplace_back([this] {
                            while (true)
                            {
                                std::function<void()> task;
                                {
                                    std::unique_lock<std::mutex> lock{m_queueMutex};
                                    m_condition.wait(lock, [this] { return m_stop || !m_tasks.empty(); });
                                    if (m_stop && m_tasks.empty())
                                        return;
                                    task = std::move(m_tasks.front());
                                    m_tasks.pop();
                                }
                                task();
                            }
                        });
                    }
                }

                template<class F>
                void enqueue(F&& f)
                {
                    {
                        std::unique_lock<std::mutex> lock{m_queueMutex};
                        if (m_stop)
                            return;
                        m_tasks.emplace(std::forward<F>(f));
                    }
                    m_condition.notify_one();
                }

                ~ThreadPool()
                {
                    {
                        std::unique_lock<std::mutex> lock{m_queueMutex};
                        m_stop = true;
                    }
                    m_condition.notify_all();
                    for (std::thread &worker : m_workers)
                        worker.join();
                }

            private:
                std::vector<std::thread> m_workers;
                std::queue<std::function<void()>> m_tasks;
                std::mutex m_queueMutex;
                std::condition_variable m_condition;
                bool m_stop;
            };


            BpmAnalysisTask::BpmAnalysisTask(std::vector<TrackId> trackIds)
                : ILongRunningTask{"Running BPM Analysis", true}, m_trackIds{std::move(trackIds)}
            {
            }

            void BpmAnalysisTask::run(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel)
            {
                theBackgroundTaskService.pause();
                try
                {
                    runInternal(progressCb, completionCb, shouldCancel);
                }
                catch (const std::exception &e)
                {
                    spdlog::error("ScanFoldersTask: Exception during scan: {}", e.what());
                }
                theBackgroundTaskService.resume();
            }

            void BpmAnalysisTask::runInternal(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> & shouldCancel)
            {
                // 1. Filter tracks to find those that actually need analysis.
                spdlog::info("BPM Analysis Task: Pre-reading track data and filtering tracks that need analysis...");
                progressCb(-1, "Querying tracks for analysis...");
                std::vector<TrackInfo> tracksToProcess;
                tracksToProcess.reserve(m_trackIds.size());
                for (const auto& trackId : m_trackIds) {
                    if (shouldCancel) break;
                    if (auto trackOpt = theTrackLibrary.getTrackDatabase()->getTrackById(trackId)) {
                        if (!trackOpt->bpm.has_value() || trackOpt->bpm.value() <= 0) {
                            tracksToProcess.push_back(*trackOpt);
                        }
                    }
                }

                if (shouldCancel) {
                    completionCb(false, "Task cancelled during track filtering.");
                    return;
                }

                const size_t totalTracks = tracksToProcess.size();
                if (totalTracks == 0)
                {
                    completionCb(true, "All selected tracks have already been analyzed.");
                    return;
                }

                // --- Configuration ---
                const unsigned int numWorkerThreads = 4;
                 // std::max(1u, std::thread::hardware_concurrency() - 1); // Leave one core for reader/writer/UI
                const size_t batchSize = 100;
                const size_t loadQueueMaxSize = numWorkerThreads * 2;

                spdlog::info("Starting BPM analysis for {} tracks using {} worker threads and 1 reader thread.", 
                    ui::formatStandardStringNumber(totalTracks),
                             ui::formatStandardStringNumber(numWorkerThreads));

                // --- Data Structures for Producer-Consumer pattern ---
                struct LoadedAudio {
                    TrackId trackId;
                    std::unique_ptr<juce::AudioBuffer<float>> buffer;
                    double sampleRate;
                };
                struct AnalysisResult {
                    TrackId trackId;
                    AudioMetadata metadata;
                };

                std::queue<LoadedAudio> loadQueue;
                std::mutex loadMutex;
                std::condition_variable loadCv;

                std::queue<AnalysisResult> resultsQueue;
                std::mutex resultsMutex;
                std::condition_variable resultsCv;
                
                std::atomic<bool> readerFinished = false;
                std::atomic<size_t> analysisCompletedCount = 0;

                // --- Reader Thread ---
                std::thread readerThread([&] {
                    juce::AudioFormatManager formatManager;
                    formatManager.registerBasicFormats();

                    for (const auto& trackInfo : tracksToProcess) {
                        if (shouldCancel) break;

                        try
                        {
                            spdlog::info("Loading audio for track ID: {} ({})", trackInfo.trackId, pathToString(trackInfo.filepath));
                            juce::File audioFile{ui::jucePathFromFs(trackInfo.filepath)};
                            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(audioFile));

                            if (reader) {
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
                                
                                auto buffer = std::make_unique<juce::AudioBuffer<float>>(
                                    static_cast<int>(reader->numChannels),
                                    numSamplesToRead
                                );
                                reader->read(buffer.get(), 0, numSamplesToRead, startSample, true, true);
                                
                                {
                                    std::unique_lock<std::mutex> lock(loadMutex);
                                    loadCv.wait(lock, [&]{ return loadQueue.size() < loadQueueMaxSize || shouldCancel; });
                                    if (shouldCancel) break;
                                    loadQueue.push({trackInfo.trackId, std::move(buffer), reader->sampleRate});
                                }
                                loadCv.notify_one();
                            }
                            else
                            {
                                spdlog::error("Failed to create reader for track ID {}", trackInfo.trackId);
                            }
                        }
                        catch (const std::exception& e)
                        {
                            spdlog::error("Exception reading audio for track ID {}: {}", trackInfo.trackId, e.what());
                        }
                        catch (...)
                        {
                            spdlog::error("Unknown exception reading audio for track ID {}", trackInfo.trackId);
                        }

                        spdlog::info("Done with track ID: {} ({})", trackInfo.trackId, pathToString(trackInfo.filepath));
                    }
                    readerFinished = true;
                    loadCv.notify_all(); // Wake up any waiting workers to let them finish
                });


                // --- Worker Threads ---
                ThreadPool pool{numWorkerThreads};
                for(size_t i = 0; i < numWorkerThreads; ++i) {
                    pool.enqueue([&] {
                        while (true) {
                            std::unique_ptr<LoadedAudio> loaded;
                            {
                                std::unique_lock<std::mutex> lock{loadMutex};
                                loadCv.wait(lock, [&]{ return !loadQueue.empty() || readerFinished; });

                                if (loadQueue.empty() && readerFinished) {
                                    break; // All done
                                }
                                loaded = std::make_unique<LoadedAudio>(std::move(loadQueue.front()));
                                loadQueue.pop();
                            }
                            loadCv.notify_one(); // Notify reader thread that there's space in the queue

                            if (loaded) {
                                spdlog::info("Analyzing audio for track ID: {}", loaded->trackId);
                                AudioMetadata am = analyzeAudioBuffer(*loaded->buffer, loaded->sampleRate);
                                spdlog::info("Done analyzing audio for track ID: {}", loaded->trackId);
                                {
                                    std::lock_guard<std::mutex> lock(resultsMutex);
                                    resultsQueue.push({loaded->trackId, am});
                                    spdlog::info("Pushed audio for track ID: {}", loaded->trackId);
                                }
                                analysisCompletedCount++;
                                resultsCv.notify_one();
                            }
                        }
                    });
                }

                // --- Writer and Progress Loop ---
                size_t tracksWritten = 0;
                std::vector<std::pair<TrackId, AudioMetadata>> resultsBatch;
                resultsBatch.reserve(batchSize);

                while (tracksWritten < totalTracks)
                {
                    if (shouldCancel) break;

                    {
                        std::unique_lock<std::mutex> lock{resultsMutex};
                        resultsCv.wait(lock, [&]{ return !resultsQueue.empty() || (analysisCompletedCount == totalTracks && resultsQueue.empty()); });

                        while (!resultsQueue.empty()) {
                            resultsBatch.emplace_back(resultsQueue.front().trackId, resultsQueue.front().metadata);
                            resultsQueue.pop();
                            if (resultsBatch.size() >= batchSize) break;
                        }
                    }

                    if (!resultsBatch.empty())
                    {
                        theTrackLibrary.getTrackDatabase()->updateTrackBpm(resultsBatch);
                        tracksWritten += resultsBatch.size();
                        resultsBatch.clear();
                    }
                    
                    int progressPercent = static_cast<int>((static_cast<float>(tracksWritten) / totalTracks) * 100.0f);
                    const auto status{std::format("Analyzed {} / {} tracks...", 
                         ui::formatStandardStringNumber(tracksWritten), 
                         ui::formatStandardStringNumber(totalTracks))};
                    progressCb(progressPercent, status);
                    
                    if (tracksWritten == totalTracks) break;
                }

                // --- Cleanup ---
                shouldCancel = true; // Signal all threads to stop
                loadCv.notify_all();
                resultsCv.notify_all();
                readerThread.join();
                // ThreadPool destructor will join workers

                if (tracksWritten < totalTracks)
                {
                    std::string finalStatus = std::format("Cancelled after analyzing {} / {} tracks.", 
                         ui::formatStandardStringNumber(tracksWritten), 
                         ui::formatStandardStringNumber(totalTracks));
                    completionCb(false, finalStatus);
                }
                else
                {
                    std::string finalStatus = std::format("Successfully analyzed {} tracks.", 
                         ui::formatStandardStringNumber(totalTracks));
                    progressCb(100, finalStatus);
                    completionCb(true, finalStatus);
                }
            }
        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio