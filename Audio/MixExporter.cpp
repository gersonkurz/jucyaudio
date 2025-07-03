/*
  ==============================================================================

    MixExporter.cpp
    Created: 1 Jun 2025 8:08:15pm
    Author:  GersonKurz

  ==============================================================================
*/

#include <Audio/ExportMixToWav.h>
#include <Audio/MixExporter.h>
#include <Database/Includes/Constants.h>
#include <Database/Includes/IMixManager.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <JuceHeader.h>
#include <Utils/AssortedUtils.h>
#include <spdlog/fmt/chrono.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace audio
    {
        using namespace jucyaudio::database;

        bool MixExporter::exportMixToFile(MixId mixId, const TrackLibrary &trackLibrary, const std::filesystem::path &targetFilePath,
                                          MixExporterProgressCallback progressCallback) const
        {
            ExportMixImplementation *implementation = nullptr;

            const auto targetExtension{getLowercaseExtension(targetFilePath)};
            if (targetExtension == ".mp3")
            {
                implementation = new ExportMp3MixImplementation{mixId, trackLibrary, targetFilePath, progressCallback};
            }
            else if (targetExtension == ".wav")
            {
                implementation = new ExportWavMixImplementation{mixId, trackLibrary, targetFilePath, progressCallback};
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
        }
    } // namespace audio
} // namespace jucyaudio
