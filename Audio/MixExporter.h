#pragma once

#include <Audio/Includes/IMixExporter.h>
#include <Database/Includes/Constants.h>
#include <Database/Includes/IMixManager.h>
#include <Database/Includes/ITrackDatabase.h>
#include <Database/TrackLibrary.h>
#include <filesystem> // For file_size return type (uintmax_t)

namespace jucyaudio
{
    namespace audio
    {

        class MixExporter final : public IMixExporter
        {
        public:
            virtual ~MixExporter() = default;

            // @brief Exports a defined mix to an audio file.
            // @param mixId The ID of the mix to export (fetches definition from IMixManager).
            // @param targetFilepath The full path where the exported audio file should be saved.
            // @param progressCallback Optional callback for progress updates.
            // @return True if export was successful, false otherwise.
            bool exportMixToFile(MixId mixId, const std::filesystem::path &targetFilepath,
                                 MixExporterProgressCallback progressCallback = nullptr
                                 // Potentially add format parameters later
            ) const override;
        };

    } // namespace audio
} // namespace jucyaudio
