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
        /// @brief Not final, so the self test can subclass it. Nothing else about it is opened up.
        ///
        /// A write that fails part way through a render is the one export failure nothing could induce
        /// from outside: every injection reachable through exportMixToFile - a read-only target, a
        /// partial path that is a directory, an unwritable parent - is refused at the setup step,
        /// before a single sample is written. So the propagation from a refused write to a failed
        /// export, and from there to the previous export surviving untouched, went unproven.
        ///
        /// The self test overrides onSetupAudioFormatManagerAndWriter to build the same WavAudioFormat
        /// writer over a stream that accepts a while and then refuses. Everything after that is this
        /// class's own code: the real mixing loop, the real write check, the real releaseOutput, and
        /// run()'s real decision to discard rather than commit.
        ///
        /// The overrides below stay private, because a derived class may override a private virtual -
        /// access controls who can name a function, not who can replace it. So the whole cost of being
        /// testable here is one keyword removed, and there is no test-only hook: the virtual the test
        /// replaces is the one the two formats already needed between themselves.
        class ExportWavMixImplementation : public ExportMixImplementation
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
