/*
  ==============================================================================

    MixExporter.cpp
    Created: 1 Jun 2025 8:08:15pm
    Author:  GersonKurz

  ==============================================================================
*/

#include <Audio/ExportMixImplementation.h>
#include <Audio/ExportMixToWav.h>
#include <Audio/ExportMixToMp3.h>
#include <Audio/ExportMixToM3U.h>
#include <Audio/MixExporter.h>
#include <Database/Includes/Constants.h>
#include <Database/Includes/IMixManager.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <Utils/AssortedUtils.h>
#include <spdlog/fmt/chrono.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace audio
    {
        using namespace jucyaudio::database;

        bool MixExporter::exportMixToFile(MixId mixId, const std::filesystem::path &targetFilePath,
                                          MixExporterProgressCallback progressCallback) const
        {
#if MIX_TRANSITION_EXPORT_AVAILABLE
            const auto targetExtension{getLowercaseExtension(targetFilePath)};
            
            // Handle M3U export separately (doesn't use ExportMixImplementation)
            if (targetExtension == ".m3u")
            {
                ExportMixToM3U m3uExporter;
                return m3uExporter.exportMix(mixId, targetFilePath, progressCallback);
            }

            // Handle audio format exports
            ExportMixImplementation *implementation = nullptr;

            if (targetExtension == ".mp3")
            {
                implementation = new ExportMp3MixImplementation{mixId, targetFilePath, progressCallback};
            }
            else if (targetExtension == ".wav")
            {
                implementation = new ExportWavMixImplementation{mixId, targetFilePath, progressCallback};
            }
            else
            {
                spdlog::error("MTE: Unsupported output file extension: {}", targetExtension);
                if (progressCallback)
                    progressCallback(1.0f, "Error: Unsupported output format.");
                return false;
            }
            assert(implementation != nullptr && "Implementation should not be null for valid extensions");
            spdlog::info("MTE: Initializing export for mix {} -> {}", mixId, pathToString(targetFilePath));
            const auto success{implementation->run()};
            delete implementation; // Clean up the implementation
            return success;
#else
            return false;
#endif // MIX_TRANSITION_EXPORT_AVAILABLE
        }
    } // namespace audio
} // namespace jucyaudio
