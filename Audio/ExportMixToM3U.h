#pragma once

#include <Database/Includes/Constants.h>
#include <filesystem>
#include <functional>
#include <string>

#if MIX_TRANSITION_EXPORT_AVAILABLE
namespace jucyaudio
{
    namespace audio
    {
        using M3UExporterProgressCallback = std::function<void(float /*progress 0.0-1.0*/, const std::string & /*statusMsg*/)>;

        class ExportMixToM3U final
        {
        public:
            ExportMixToM3U() = default;
            ~ExportMixToM3U() = default;

            // Non-copyable, non-movable
            ExportMixToM3U(const ExportMixToM3U &) = delete;
            ExportMixToM3U &operator=(const ExportMixToM3U &) = delete;
            ExportMixToM3U(ExportMixToM3U &&) = delete;
            ExportMixToM3U &operator=(ExportMixToM3U &&) = delete;

            // @brief Exports a mix to M3U playlist format with extended comments
            // @param mixId The ID of the mix to export
            // @param targetFilepath The full path where the M3U file should be saved
            // @param progressCallback Optional callback for progress updates
            // @return True if export was successful, false otherwise
            bool exportMix(MixId mixId, const std::filesystem::path &targetFilepath,
                          M3UExporterProgressCallback progressCallback = nullptr) const;
        };

    } // namespace audio
} // namespace jucyaudio
#endif