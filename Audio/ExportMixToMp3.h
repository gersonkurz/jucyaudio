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

        class ExportMp3MixImplementation final : public ExportMixImplementation
        {
        public:
            ExportMp3MixImplementation(MixId mixId, const TrackLibrary &trackLibrary, const std::filesystem::path &targetFilepath,
                                       MixExporterProgressCallback progressCallback)
                : ExportMixImplementation(mixId, trackLibrary, targetFilepath, progressCallback)
            {
            }
            JUCE_DECLARE_NON_COPYABLE(ExportMp3MixImplementation)
        private:
            bool onSetupAudioFormatManagerAndWriter() override;
            bool onRunMixingLoop() override; // Override to use LAME instead of JUCE writer

            // LAME-specific members
            lame_global_flags *m_lameFlags = nullptr;
            std::unique_ptr<juce::FileOutputStream> m_outputStream;
            unsigned char *m_mp3Buffer = nullptr;
            int m_mp3BufferSize = 0;

            // Override destructor to clean up LAME
            ~ExportMp3MixImplementation() override;
        };

    } // namespace audio
} // namespace jucyaudio
