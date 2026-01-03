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

        ExportResult MixExporter::exportMixToFile(MixId mixId, const audio::ActiveExportSettings &settings, MixExporterProgressCallback progressCallback) const
        {
            const auto targetExtension{getLowercaseExtension(settings.outputPath)};

            // Handle M3U export separately (doesn't use ExportMixImplementation)
            if (targetExtension == ".m3u")
            {
                ExportMixToM3U m3uExporter{};
                bool success = m3uExporter.exportMix(mixId, settings.outputPath, progressCallback);
                return success ? ExportResult::Success() : ExportResult::Failure("M3U export failed");
            }

            // Handle audio format exports
            spdlog::info("MTE: Initializing export for mix {} -> {}", mixId, pathToString(settings.outputPath));

            ExportResult result;
            if (targetExtension == ".mp3")
            {
                ExportMp3MixImplementation implementation{mixId, settings, progressCallback};
                result = implementation.run();
            }
            else if (targetExtension == ".wav")
            {
                ExportWavMixImplementation implementation{mixId, settings, progressCallback};
                result = implementation.run();
            }
            else
            {
                spdlog::error("MTE: Unsupported output file extension: {}", targetExtension);
                if (progressCallback)
                    progressCallback(1.0f, "Error: Unsupported output format.");
                return ExportResult::Failure("Unsupported output format: " + targetExtension);
            }

            // Mark the mix as exported if successful
            if (result.success && !settings.exportFolder.empty())
            {
                const auto& mixManager = database::theTrackLibrary.getMixManager();
                mixManager.setMixExported(mixId, settings.exportFolder);
                spdlog::info("Marked mix {} as Exported to folder '{}'", mixId, settings.exportFolder);
            }
            else if (result.success)
            {
                // Legacy: If no folder specified, just mark as exported
                const auto& mixManager = database::theTrackLibrary.getMixManager();
                mixManager.setMixStatus(mixId, "Exported");
                spdlog::info("Marked mix {} as Exported (no folder)", mixId);
            }

            return result;
        }
    } // namespace audio
} // namespace jucyaudio
