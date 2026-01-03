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

        // Structured result from export operations, allowing callers to see warnings
        struct ExportResult
        {
            bool success{false};
            int warningCount{0};
            std::string message;

            // Convenience constructors
            static ExportResult Success(int warnings = 0)
            {
                return {true, warnings, ""};
            }

            static ExportResult Failure(const std::string &msg)
            {
                return {false, 0, msg};
            }

            explicit operator bool() const { return success; }
        };

        class IMixExporter
        {
        public:
            virtual ~IMixExporter() = default;

            virtual ExportResult exportMixToFile(MixId mixId, const ActiveExportSettings &settings, MixExporterProgressCallback progressCallback = nullptr) const = 0;
        };

    } // namespace audio
} // namespace jucyaudio
