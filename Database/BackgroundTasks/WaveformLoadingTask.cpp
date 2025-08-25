#include <Database/BackgroundTasks/WaveformLoadingTask.h>
#include <Database/TrackLibrary.h>
#include <Utils/UiUtils.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <format>

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
                    bool success = loadWaveformFromFile(request, progressCb);
                    
                    if (success)
                    {
                        m_successCount++;
                        spdlog::info("[WaveformLoadingTask] Successfully loaded waveform for track {}", request.trackId);
                    }
                    else
                    {
                        m_failedTracks.push_back(request.trackId);
                        spdlog::warn("[WaveformLoadingTask] Failed to load waveform for track {}", request.trackId);
                    }
                    
                    processed++;
                }
                
                // Final status
                std::string finalMessage;
                if (m_failedTracks.empty())
                {
                    finalMessage = std::format("Successfully loaded {} waveforms ({} cached)", 
                                              m_successCount, m_cachedCount);
                }
                else
                {
                    finalMessage = std::format("Loaded {} of {} waveforms ({} failed, {} cached)", 
                                              m_successCount, needLoadingCount, 
                                              m_failedTracks.size(), m_cachedCount);
                }
                
                spdlog::info("[WaveformLoadingTask] Completed: {}", finalMessage);
                completionCb(m_failedTracks.empty(), finalMessage);
            }

            bool WaveformLoadingTask::loadWaveformFromFile(const WaveformRequest& request,
                                                          ProgressCallback progressCb)
            {
                // Check if file exists
                if (!std::filesystem::exists(request.filePath))
                {
                    spdlog::error("[WaveformLoadingTask] File not found: {}", request.filePath.string());
                    return false;
                }
                
                juce::File audioFile{ui::jucePathFromFs(request.filePath)};
                if (!audioFile.existsAsFile())
                {
                    spdlog::error("[WaveformLoadingTask] JUCE File not found: {}", audioFile.getFullPathName().toStdString());
                    return false;
                }
                
                // Try to generate and cache the waveform
                return generateAndCacheWaveform(request.trackId, audioFile, progressCb);
            }

            bool WaveformLoadingTask::generateAndCacheWaveform(TrackId trackId,
                                                              const juce::File& audioFile,
                                                              ProgressCallback progressCb)
            {
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
                            return false;
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
                            return true;
                        }
                        else
                        {
                            spdlog::error("[WaveformLoadingTask] Failed to save waveform to cache for track {}", 
                                         trackId);
                            return false;
                        }
                    }
                    else
                    {
                        spdlog::error("[WaveformLoadingTask] Generated empty waveform for track {}", trackId);
                        return false;
                    }
                }
                catch (const std::exception& e)
                {
                    spdlog::error("[WaveformLoadingTask] Exception generating waveform for track {}: {}", 
                                 trackId, e.what());
                    return false;
                }
                catch (...)
                {
                    spdlog::error("[WaveformLoadingTask] Unknown exception generating waveform for track {}", trackId);
                    return false;
                }
            }

        } // namespace background_tasks
    } // namespace database
} // namespace jucyaudio