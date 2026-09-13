#pragma once

#include <Audio/ExportMixImplementation.h>
#include <Audio/Includes/IMixExporter.h>
#include <Audio/MixExporter.h>
#include <Database/Includes/Constants.h>
#include <Database/Includes/IMixManager.h>
#include <Database/Includes/ITrackDatabase.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <filesystem>
#include <lame.h>
#include <vector>
#include <spdlog/fmt/chrono.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace audio
    {
        using namespace database;
        class ExportMp3MixImplementation final : public ExportMixImplementation
        {
        public:
            ExportMp3MixImplementation(MixId mixId, const ActiveExportSettings &settings, MixExporterProgressCallback progressCallback)
                : ExportMixImplementation{mixId, settings, progressCallback}
            {
            }
            ~ExportMp3MixImplementation() override;
            JUCE_DECLARE_NON_COPYABLE(ExportMp3MixImplementation)
        private:
            bool onSetupAudioFormatManagerAndWriter() override;
            bool onRunMixingLoop() override; // Override to use LAME instead of JUCE writer

            /// @brief This one writes through its own stream rather than the base's writer, and the
            ///        rendered file cannot be moved into place while it is open.
            ///
            /// The flush is the point, not the reset. The LAME info frame and the ID3v1 footer are
            /// written after the encoder's last flush and are small enough to sit in the stream's
            /// buffer, so their write() calls return true without reaching the OS. Destroying the
            /// stream flushes them and throws the result away. So the flush happens here, where its
            /// answer can still be given back, and getStatus is asked afterwards because it carries
            /// the first failure the stream saw rather than only the last.
            bool releaseOutput() override
            {
                ExportMixImplementation::releaseOutput();
                if (!m_outputStream)
                {
                    return true;
                }

                m_outputStream->flush();
                const auto status = m_outputStream->getStatus();
                m_outputStream.reset();
                if (status.failed())
                {
                    spdlog::error("MTE: the MP3 could not be written out completely: {}", status.getErrorMessage().toStdString());
                    return false;
                }
                return true;
            }

            // LAME-specific members
            lame_global_flags *m_lameFlags = nullptr;
            std::unique_ptr<juce::FileOutputStream> m_outputStream;
            std::vector<unsigned char> m_mp3Buffer;

            /// @brief Where in the file LAME's placeholder tag frame sits, so the finished one can
            ///        replace it.
            ///
            /// lame.h is explicit about this: "LAME inserted an empty frame in the beginning of mp3
            /// audio data, which you have to replace by the final LAME-tag frame after encoding. In
            /// case there is no ID3v2 tag, usually this frame will be the very first data in your mp3
            /// file. If you put some other leading data into your file, you'll have to do some
            /// bookkeeping about where to write this buffer."
            ///
            /// This exporter writes an ID3v2 tag first, so it is exactly the case that comment
            /// describes, and this member is that bookkeeping. Negative until the ID3v2 tag has been
            /// written and the position is known.
            juce::int64 m_lameTagFrameOffset{-1};
        };
    } // namespace audio
} // namespace jucyaudio
