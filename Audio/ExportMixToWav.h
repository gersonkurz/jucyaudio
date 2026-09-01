#pragma once

#include <Audio/Includes/IMixExporter.h>
#include <Audio/MixExporter.h>
#include <Audio/ExportMixImplementation.h>
#include <Database/Includes/Constants.h>
#include <Database/Includes/IMixManager.h>
#include <Database/Includes/ITrackDatabase.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/TrackLibrary.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <Utils/AssortedUtils.h>

#include <filesystem>
#include <spdlog/fmt/chrono.h>
#include <spdlog/spdlog.h>
#include <lame.h>

namespace jucyaudio
{
    namespace audio
    {
        using namespace database;
        class ExportWavMixImplementation final : public ExportMixImplementation
        {
        public:
            ExportWavMixImplementation(MixId mixId, const ActiveExportSettings &settings,
                                       MixExporterProgressCallback progressCallback)
                : ExportMixImplementation{mixId, settings, progressCallback}
            {
            }
            ~ExportWavMixImplementation() = default;
            JUCE_DECLARE_NON_COPYABLE(ExportWavMixImplementation)

        private:
            bool onSetupAudioFormatManagerAndWriter() override;
            bool onRunMixingLoop() override;

            /// @brief Destroying a WavAudioFormatWriter is not free of consequence: it seeks back and
            ///        rewrites the RIFF header with the real length, and neither that write nor the
            ///        flush behind it reports to anyone. The writer owns the stream and takes it with
            ///        it, so there is nothing left to ask afterwards - what is left is the file, and
            ///        a header patch or a buffered write that did not land leaves it short.
            bool releaseOutput() override;
        };
    } // namespace audio
} // namespace jucyaudio
