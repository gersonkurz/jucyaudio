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
#include <lame/lame.h>

namespace jucyaudio
{
    namespace audio
    {
        using namespace database;

        class ExportWavMixImplementation final : public ExportMixImplementation
        {
        public:
            ExportWavMixImplementation(MixId mixId, const std::filesystem::path &targetFilepath,
                                       MixExporterProgressCallback progressCallback)
                : ExportMixImplementation(mixId, targetFilepath, progressCallback)
            {
            }
            JUCE_DECLARE_NON_COPYABLE(ExportWavMixImplementation)

        private:
            bool onSetupAudioFormatManagerAndWriter() override;
            bool onRunMixingLoop() override;
        };

    } // namespace audio
} // namespace jucyaudio
