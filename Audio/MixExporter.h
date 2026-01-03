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

            ExportResult exportMixToFile(
                MixId mixId, const audio::ActiveExportSettings &settings, MixExporterProgressCallback progressCallback = nullptr) const override;
        };

    } // namespace audio
} // namespace jucyaudio
