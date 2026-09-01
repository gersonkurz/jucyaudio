#pragma once

#include <Audio/Includes/ActiveExportSettings.h>
#include <Database/Includes/Constants.h>
#include <Database/Includes/IMixManager.h>
#include <Database/Includes/ITrackDatabase.h>
#include <Database/TrackLibrary.h>
#include <chrono>     // For time_point
#include <cstdint>    // For std::uintmax_t
#include <filesystem> // For file_size return type (uintmax_t)
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace audio
    {
        /// @brief Reports how far an export has got, and says whether it should carry on.
        ///
        /// Returns true to keep going, false to stop. It used to return void, and the callers that
        /// wanted to cancel already computed the answer and handed it back - `return !shouldCancel;` -
        /// where std::function quietly discarded it. So the cancel button did nothing until the render
        /// finished on its own: minutes of waiting on a long mix, and then the file was written anyway.
        ///
        /// The return type is what makes that hard to reintroduce. A lambda returning void cannot be
        /// stored in this, so a call site that forgets to answer does not compile.
        using MixExporterProgressCallback = std::function<bool(float /*progress 0.0-1.0*/, const std::string & /*statusMsg*/)>;

        // Structured result from export operations, allowing callers to see warnings
        struct ExportResult
        {
            bool success{false};
            int warningCount{0};
            std::string message;

            /**
             * @brief Set when the audio was produced but its recovery record was not.
             *
             * Deliberately separate from warningCount, which counts tracks that failed to read while
             * rendering. Those two mean opposite things to whoever is listening: one says part of your
             * audio is silent, the other says your audio is fine but nothing is written down about how it
             * was made. Sharing a field would let a real audio problem hide behind a bookkeeping one.
             *
             * Empty when there is nothing to say. Never turns a successful export into a failure - the
             * mp3 exists and plays, and telling someone their two-hour render failed would only make them
             * run it again.
             */
            std::string recoveryWarning;

            // Convenience constructors
            static ExportResult Success(int warnings = 0)
            {
                return {true, warnings, ""};
            }

            static ExportResult Failure(const std::string &msg)
            {
                return {false, 0, msg};
            }

            explicit operator bool() const { return success; }
        };

        class IMixExporter
        {
        public:
            virtual ~IMixExporter() = default;

            virtual ExportResult exportMixToFile(MixId mixId, const ActiveExportSettings &settings, MixExporterProgressCallback progressCallback = nullptr) const = 0;
        };

    } // namespace audio
} // namespace jucyaudio
