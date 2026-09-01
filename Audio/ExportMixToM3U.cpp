#include <Audio/ExportMixToM3U.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>
#include <fstream>
#include <locale>
#include <format>
#include <unordered_map>
#include <spdlog/spdlog.h>

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
            if (mixInfo.name.empty())
            {
                spdlog::error("ExportMixToM3U: Mix ID {} has no name", mixId);
                return false;
            }

            const auto mixTracks{mixManager.getMixTracks(mixId)};
            if (mixTracks.empty())
            {
                spdlog::error("ExportMixToM3U: Mix ID {} has no tracks", mixId);
                return false;
            }

            if(!theTrackLibrary.isInitialised())
            {
                spdlog::error("ExportMixToM3U: TrackLibrary is not initialised");
                return false;
            }

            // Get track database for track info lookup
            const auto& trackDb{ theTrackLibrary.getTrackDatabase() };

            // Open output file
            std::ofstream outFile{targetFilepath, std::ios::out | std::ios::binary};
            if (!outFile.is_open())
            {
                spdlog::error("ExportMixToM3U: Failed to open file for writing: {}", pathToString(targetFilepath));
                return false;
            }

            // Same reason as MixRecoveryM3U: the application sets a global locale with thousands
            // separators and operator<< honours it, so the streamed duration below came out as
            // "20,757" and read back as 20. Machine-readable file, so numbers are digits.
            outFile.imbue(std::locale::classic());

            // Write M3U header
            outFile << "#EXTM3U\n";
            outFile << "#EXTMIX:" << mixInfo.name << "\n";
            outFile << "#EXTMIXDURATION:" << std::chrono::duration_cast<std::chrono::seconds>(mixInfo.totalDuration).count() << "\n\n";

            // Calculate timeline positions using the ATTACH model, through the shared walk in
            // MixInfo.h. What is written here is the *audible* start, which is the track's position
            // plus its cueStart.
            const auto trackStarts{database::calculateMixTrackStarts(mixTracks)};
            std::vector<Duration_t> trackAudibleStartTimes;
            trackAudibleStartTimes.reserve(mixTracks.size());

            for (size_t i = 0; i < mixTracks.size(); ++i)
            {
                trackAudibleStartTimes.push_back(trackStarts[i] + mixTracks[i].cueStart);
            }

            // Process each track
            size_t processedTracks{0};
            for (size_t i = 0; i < mixTracks.size(); ++i)
            {
                const auto& mixTrack = mixTracks[i];

                if (progressCallback)
                {
                    float progress{static_cast<float>(processedTracks) / static_cast<float>(mixTracks.size())};
                    progressCallback(progress, std::format("Processing track {} of {}", processedTracks + 1, mixTracks.size()));
                }

                // Get track info
                const auto trackInfo{ trackDb.getTrackById(mixTrack.trackId) };
                if (!trackInfo.has_value())
                {
                    spdlog::warn("ExportMixToM3U: Track ID {} not found, skipping", mixTrack.trackId);
                    continue;
                }

                // Calculate duration in seconds
                const auto durationSeconds{std::chrono::duration_cast<std::chrono::seconds>(trackInfo->duration).count()};
                const auto trackPath{trackInfo->reconstructFullPath()};

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
                    // Fallback to filename without extension.
                    // pathToString, not path::string(): the narrow form of a Windows path goes through
                    // the active code page, which mangles or throws on this library's non-ASCII names.
                    artistTitle = pathToString(trackPath.stem());
                }

                // Write absolute start time as a comment
                const auto absoluteStartTime = trackAudibleStartTimes[i];
                outFile << "# Starts at: " << durationToString(absoluteStartTime) << "\n";

                // Write EXTINF line
                outFile << std::format("#EXTINF:{},{}\n", durationSeconds, artistTitle);
                
                // Write custom EXTREM line with original filename
                outFile << std::format("#EXTREM:{}\n", pathToString(trackPath.filename()));
                
                // Write file path (absolute path as stored in database)
                outFile << pathToString(trackPath) << "\n\n";

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

