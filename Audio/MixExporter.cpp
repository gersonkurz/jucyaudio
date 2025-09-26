/*
  ==============================================================================

    MixExporter.cpp
    Created: 1 Jun 2025 8:08:15pm
    Author:  GersonKurz

  ==============================================================================
*/

#include <Audio/ExportMixImplementation.h>
#include <Audio/ExportMixToM3U.h>
#include <Audio/ExportMixToMp3.h>
#include <Audio/ExportMixToWav.h>
#include <Audio/MixExporter.h>
#include <Database/Includes/Constants.h>
#include <Database/Includes/IMixManager.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <spdlog/fmt/chrono.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace audio
    {
        using namespace jucyaudio::database;

        bool MixExporter::exportMixToFile(MixId mixId, const audio::ActiveExportSettings &settings, MixExporterProgressCallback progressCallback) const
        {
            const auto targetExtension{getLowercaseExtension(settings.outputPath)};

            // Handle M3U export separately (doesn't use ExportMixImplementation)
            if (targetExtension == ".m3u")
            {
                ExportMixToM3U m3uExporter{};
                return m3uExporter.exportMix(mixId, settings.outputPath, progressCallback);
            }

            // Handle audio format exports
            ExportMixImplementation *implementation = nullptr;

            if (targetExtension == ".mp3")
            {
                implementation = new ExportMp3MixImplementation{mixId, settings, progressCallback};
            }
            else if (targetExtension == ".wav")
            {
                implementation = new ExportWavMixImplementation{mixId, settings, progressCallback};
            }
            else
            {
                spdlog::error("MTE: Unsupported output file extension: {}", targetExtension);
                if (progressCallback)
                    progressCallback(1.0f, "Error: Unsupported output format.");
                return false;
            }
            assert(implementation != nullptr && "Implementation should not be null for valid extensions");
            spdlog::info("MTE: Initializing export for mix {} -> {}", mixId, pathToString(settings.outputPath));
            const auto success{implementation->run()};
            delete implementation; // Clean up the implementation
            
            // Mark the mix as exported if successful
            if (success)
            {
                const auto& mixManager = database::theTrackLibrary.getMixManager();
                mixManager.setMixStatus(mixId, "Exported");
                spdlog::info("Marked mix {} as Exported", mixId);
            }
            
            return success;
        }
    } // namespace audio
} // namespace jucyaudio
