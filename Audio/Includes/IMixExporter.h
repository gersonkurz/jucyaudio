#pragma once

#include <Audio/Includes/ActiveExportSettings.h>
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

            virtual bool exportMixToFile(MixId mixId, const ActiveExportSettings &settings, MixExporterProgressCallback progressCallback = nullptr) const = 0;
        };

    } // namespace audio
} // namespace jucyaudio
