#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/IMixManager.h>
#include <Database/Includes/ITrackDatabase.h>
#include <Database/TrackLibrary.h>
#include <chrono>     // For time_point
#include <cstdint>    // For std::uintmax_t
#include <filesystem> // For file_size return type (uintmax_t)
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace audio
    {
        using MixExporterProgressCallback = std::function<void(float /*progress 0.0-1.0*/, const std::string & /*statusMsg*/)>;

        class IMixExporter
        {
        public:
            virtual ~IMixExporter() = default;

            // @brief Exports a defined mix to an audio file.
            // @param mixId The ID of the mix to export (fetches definition from IMixManager).
            // @param targetFilepath The full path where the exported audio file should be saved.
            // @param progressCallback Optional callback for progress updates.
            // @return True if export was successful, false otherwise.
            virtual bool exportMixToFile(MixId mixId, const std::filesystem::path &targetFilepath,
                                         MixExporterProgressCallback progressCallback = nullptr
                                         // Potentially add format parameters later
                                         ) const = 0;
        };

    } // namespace database
} // namespace jucyaudio
