#pragma once

#include <Database/Includes/Constants.h>
#include <Audio/Includes/IMixExporter.h>
#include <Audio/AudioLibrary.h>
#include <Audio/MixExporter.h>
#include <filesystem> // For std::filesystem::path
#include <memory>     // For std::unique_ptr
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace audio
    {
        class AudioLibrary final
        {
        public:
            AudioLibrary() = default;
            ~AudioLibrary() = default;

            // Non-copyable
            AudioLibrary(const AudioLibrary &) = delete;
            AudioLibrary &operator=(const AudioLibrary &) = delete;
            // Movable (if needed, though for a central engine, maybe not)
            AudioLibrary(AudioLibrary &&) = delete;
            AudioLibrary &operator=(AudioLibrary &&) = delete;

            const IMixExporter &getMixExporter() const
            {
                return m_mixExporter;
            }

        private:
            MixExporter m_mixExporter;
        };

    } // namespace database
} // namespace jucyaudio
