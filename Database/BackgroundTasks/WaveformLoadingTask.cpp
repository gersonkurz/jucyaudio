#include <Database/BackgroundTasks/WaveformLoadingTask.h>
#include <Database/TrackLibrary.h>
#include <Utils/UiUtils.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <format>
#include <sstream>

namespace jucyaudio
{
    namespace database
    {
        namespace background_tasks
        {
            WaveformLoadingTask::WaveformLoadingTask(
                std::vector<WaveformRequest> requests,
                juce::AudioFormatManager& formatManager,
                juce::AudioThumbnailCache& thumbnailCache)
                : ILongRunningTask("Loading Waveforms", true),  // cancellable
                  m_requests{std::move(requests)},
                  m_formatManager{formatManager},
                  m_thumbnailCache{thumbnailCache}
            {
                spdlog::info("[WaveformLoadingTask] Created with {} requests", m_requests.size());
            }

            void WaveformLoadingTask::run(ProgressCallback progressCb, 
                                         CompletionCallback completionCb, 
                                         std::atomic<bool>& shouldCancel)
            {
                spdlog::info("[WaveformLoadingTask] Starting to load {} waveforms", m_requests.size());
                
                if (m_requests.empty())
                {
                    completionCb(true, "No waveforms to load");
                    return;
                }
                
                // Count how many actually need loading
                int needLoadingCount = 0;
                for (const auto& request : m_requests)
                {
                    if (request.needsLoading)
                    {
                        needLoadingCount++;
                    }
                    else
                    {
                        m_cachedCount++;
                    }
                }
                
                if (needLoadingCount == 0)
                {
                    spdlog::info("[WaveformLoadingTask] All {} waveforms already cached", m_cachedCount);
                    completionCb(true, std::format("All {} waveforms already cached", m_cachedCount));
                    return;
                }
                
                spdlog::info("[WaveformLoadingTask] Loading {} waveforms ({} already cached)", 
                            needLoadingCount, m_cachedCount);
                
                // Process each waveform request
                int processed = 0;
                int loadedCount = 0;  // Counter for waveforms actually being loaded (not cached)
                for (const auto& request : m_requests)
                {
                    // Check for cancellation
                    if (shouldCancel.load())
                    {
                        spdlog::info("[WaveformLoadingTask] Cancelled by user after processing {} waveforms", processed);
                        completionCb(false, std::format("Cancelled. Loaded {} of {} waveforms", 
                                                       m_successCount, needLoadingCount));
                        return;
                    }
                    
                    // Skip if already cached
                    if (!request.needsLoading)
                    {
                        processed++;
                        continue;
                    }
                    
                    // Increment counter for waveforms being loaded
                    loadedCount++;
                    
                    // Calculate progress percentage based on waveforms that need loading
                    // loadedCount-1 because we just incremented it but haven't loaded yet
                    // After loading, it will be loadedCount out of needLoadingCount
                    int progressPercent = ((loadedCount - 1) * 100) / needLoadingCount;
                    std::string statusMessage = std::format("Loading waveform {} of {}: {}", 
                                                           loadedCount,
                                                           needLoadingCount,
                                                           request.trackName);
                    progressCb(progressPercent, statusMessage);
                    
                    // Load the waveform
                    const auto failure = loadWaveformFromFile(request, progressCb);

                    if (!failure.has_value())
                    {
                        m_successCount++;
                        spdlog::info("[WaveformLoadingTask] Successfully loaded waveform for track {}", request.trackId);
                    }
                    else
                    {
                        m_failedTracks.push_back(FailedWaveform{
                            request.trackId,
                            request.trackName,
                            request.filePath,
                            failure->reason,
                            failure->kind});
                        spdlog::warn("[WaveformLoadingTask] Failed to load waveform for track {} ({}): {}",
                                     request.trackId, request.trackName, failure->reason);
                    }
                    
                    processed++;
                }
                
                // Final status
                const auto finalMessage = buildFinalMessage(needLoadingCount);
                
                spdlog::info("[WaveformLoadingTask] Completed: {}", finalMessage);
                // The task itself succeeded: it examined every request. Individual failures are reported
                // through getFailedTracks(), which the caller turns into a readable list. Reporting
                // failure here would only pin a progress dialog open on a one-line summary.
                completionCb(true, finalMessage);
            }

            std::optional<WaveformLoadingTask::FailureDetail> WaveformLoadingTask::loadWaveformFromFile(const WaveformRequest& request,
                                                                                                         ProgressCallback progressCb)
            {
                (void) progressCb;

                // Check if file exists
                if (!std::filesystem::exists(request.filePath))
                {
                    spdlog::error("[WaveformLoadingTask] File not found: {}", request.filePath.string());
                    return FailureDetail{FailureKind::FileMissing, "file not found"};
                }
                
                juce::File audioFile{ui::jucePathFromFs(request.filePath)};
                if (!audioFile.existsAsFile())
                {
                    spdlog::error("[WaveformLoadingTask] JUCE File not found: {}", audioFile.getFullPathName().toStdString());
                    return FailureDetail{FailureKind::FileMissing, "file not found"};
                }
                
                // Try to generate and cache the waveform
                return generateAndCacheWaveform(request.trackId, audioFile, progressCb);
            }

            std::optional<WaveformLoadingTask::FailureDetail> WaveformLoadingTask::generateAndCacheWaveform(TrackId trackId,
                                                                                                             const juce::File& audioFile,
                                                                                                             ProgressCallback progressCb)
            {
                (void) progressCb;

                try
                {
                    // Create a thumbnail for loading
                    juce::AudioThumbnail thumbnail{512, m_formatManager, m_thumbnailCache};
                    
                    // Set the source file
                    thumbnail.setSource(new juce::FileInputSource{audioFile});
                    
                    // Wait for loading to complete (with timeout)
                    auto startTime = std::chrono::steady_clock::now();
                    while (!thumbnail.isFullyLoaded())
                    {
                        // Check timeout
                        auto elapsed = std::chrono::steady_clock::now() - startTime;
                        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > LOAD_TIMEOUT_MS)
                        {
                            spdlog::error("[WaveformLoadingTask] Timeout loading waveform for track {}", trackId);
                            return FailureDetail{FailureKind::Timeout, std::format("timed out after {} seconds", LOAD_TIMEOUT_MS / 1000)};
                        }
                        
                        // Small sleep to avoid busy waiting
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                    
                    // Save to cache
                    juce::MemoryOutputStream stream;
                    thumbnail.saveTo(stream);
                    
                    if (stream.getDataSize() > 0)
                    {
                        const auto* data = static_cast<const unsigned char*>(stream.getData());
                        std::vector<unsigned char> waveformData(data, data + stream.getDataSize());
                        
                        auto& db = theTrackLibrary;
                        auto result = db.saveWaveform(trackId, waveformData);
                        
                        if (result.isOk())
                        {
                            spdlog::info("[WaveformLoadingTask] Cached waveform for track {} ({} bytes)", 
                                        trackId, waveformData.size());
                            return std::nullopt;
                        }
                        else
                        {
                            spdlog::error("[WaveformLoadingTask] Failed to save waveform to cache for track {}: {}",
                                         trackId, result.errorMessage);
                            // The audio decoded; only writing the cache row failed. Not the file's fault.
                            return FailureDetail{FailureKind::CacheWriteFailed, "failed to save waveform cache"};
                        }
                    }
                    else
                    {
                        spdlog::error("[WaveformLoadingTask] Generated empty waveform for track {}", trackId);
                        return FailureDetail{FailureKind::EmptyWaveform, "generated empty waveform"};
                    }
                }
                catch (const std::exception& e)
                {
                    spdlog::error("[WaveformLoadingTask] Exception generating waveform for track {}: {}", 
                                 trackId, e.what());
                    return FailureDetail{FailureKind::DecodeFailed, std::format("decoder error: {}", e.what())};
                }
                catch (...)
                {
                    spdlog::error("[WaveformLoadingTask] Unknown exception generating waveform for track {}", trackId);
                    return FailureDetail{FailureKind::DecodeFailed, "unknown decoder error"};
                }
            }

            std::string WaveformLoadingTask::buildFinalMessage(int needLoadingCount) const
            {
                if (m_failedTracks.empty())
                {
                    return std::format("Successfully loaded {} waveform{} ({} cached)",
                                       m_successCount,
                                       m_successCount == 1 ? "" : "s",
                                       m_cachedCount);
                }

                std::ostringstream message;
                message << "Failed to load " << m_failedTracks.size() << " waveform"
                        << (m_failedTracks.size() == 1 ? "" : "s");

                if (needLoadingCount > 0)
                {
                    message << " (" << m_successCount << "/" << needLoadingCount << " loaded";
                    if (m_cachedCount > 0)
                    {
                        message << ", " << m_cachedCount << " cached";
                    }
                    message << ")";
                }

                message << ": ";

                constexpr size_t maxFailuresToList = 3;
                for (size_t i = 0; i < m_failedTracks.size() && i < maxFailuresToList; ++i)
                {
                    if (i > 0)
                    {
                        message << "; ";
                    }
                    message << buildFailureLabel(m_failedTracks[i]);
                }

                if (m_failedTracks.size() > maxFailuresToList)
                {
                    message << std::format("; and {} more", m_failedTracks.size() - maxFailuresToList);
                }

                return message.str();
            }

            std::string WaveformLoadingTask::buildFailureLabel(const FailedWaveform& failure) const
            {
                const auto displayName = !failure.trackName.empty()
                    ? failure.trackName
                    : failure.filePath.filename().string();

                if (!displayName.empty())
                {
                    return std::format("{} ({})", displayName, failure.reason);
                }

                return std::format("track {} ({})", failure.trackId, failure.reason);
            }

        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio
