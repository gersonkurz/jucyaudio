#include <Audio/ExportMixToM3U.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>
#include <fstream>
#include <format>
#include <spdlog/spdlog.h>

#if MIX_TRANSITION_EXPORT_AVAILABLE
namespace jucyaudio
{
    namespace audio
    {
        bool ExportMixToM3U::exportMix(MixId mixId, const std::filesystem::path &targetFilepath,
                                       M3UExporterProgressCallback progressCallback) const
        {
            using namespace database;
            
            // Get mix info and tracks
            const auto &mixManager{theTrackLibrary.getMixManager()};
            const auto mixInfo{mixManager.getMix(mixId)};
            if (!mixInfo.name.empty())
            {
                spdlog::error("ExportMixToM3U: Mix ID {} not found", mixId);
                return false;
            }

            const auto mixTracks{mixManager.getMixTracks(mixId)};
            if (mixTracks.empty())
            {
                spdlog::error("ExportMixToM3U: Mix ID {} has no tracks", mixId);
                return false;
            }

            // Get track database for track info lookup
            const auto *trackDb{theTrackLibrary.getTrackDatabase()};
            if (!trackDb)
            {
                spdlog::error("ExportMixToM3U: Track database not available");
                return false;
            }

            // Open output file
            std::ofstream outFile{targetFilepath, std::ios::out | std::ios::binary};
            if (!outFile.is_open())
            {
                spdlog::error("ExportMixToM3U: Failed to open file for writing: {}", pathToString(targetFilepath));
                return false;
            }

            // Write M3U header
            outFile << "#EXTM3U\n";
            outFile << "#EXTMIX:" << mixInfo.name << "\n";
            outFile << "#EXTMIXDURATION:" << std::chrono::duration_cast<std::chrono::seconds>(mixInfo.totalDuration).count() << "\n\n";

            // Process each track
            size_t processedTracks{0};
            for (const auto &mixTrack : mixTracks)
            {
                if (progressCallback)
                {
                    float progress{static_cast<float>(processedTracks) / static_cast<float>(mixTracks.size())};
                    progressCallback(progress, std::format("Processing track {} of {}", processedTracks + 1, mixTracks.size()));
                }

                // Get track info
                const auto trackInfo{trackDb->getTrackById(mixTrack.trackId)};
                if (!trackInfo.has_value())
                {
                    spdlog::warn("ExportMixToM3U: Track ID {} not found, skipping", mixTrack.trackId);
                    continue;
                }

                // Calculate duration in seconds
                const auto durationSeconds{std::chrono::duration_cast<std::chrono::seconds>(trackInfo->duration).count()};
                
                // Build artist - title string
                std::string artistTitle;
                if (!trackInfo->artist_name.empty() && !trackInfo->title.empty())
                {
                    artistTitle = std::format("{} - {}", trackInfo->artist_name, trackInfo->title);
                }
                else if (!trackInfo->title.empty())
                {
                    artistTitle = trackInfo->title;
                }
                else
                {
                    // Fallback to filename without extension
                    artistTitle = trackInfo->filepath.stem().string();
                }

                // Write EXTINF line
                outFile << std::format("#EXTINF:{},{}\n", durationSeconds, artistTitle);
                
                // Write custom EXTREM line with original filename
                outFile << std::format("#EXTREM:{}\n", trackInfo->filepath.filename().string());
                
                // Write EXTSTART line with mix start time in seconds
                const auto startSeconds{std::chrono::duration_cast<std::chrono::milliseconds>(mixTrack.mixStartTime).count() / 1000.0};
                outFile << std::format("#EXTSTART:{:.1f}\n", startSeconds);
                
                // Write file path (absolute path as stored in database)
                outFile << pathToString(trackInfo->filepath) << "\n\n";

                processedTracks++;
            }

            outFile.close();

            if (progressCallback)
            {
                progressCallback(1.0f, "M3U export completed");
            }

            spdlog::info("ExportMixToM3U: Successfully exported {} tracks to {}", processedTracks, pathToString(targetFilepath));
            return true;
        }

    } // namespace audio
} // namespace jucyaudio
#endif

