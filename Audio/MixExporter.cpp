/*
  ==============================================================================

    MixExporter.cpp
    Created: 1 Jun 2025 8:08:15pm
    Author:  GersonKurz

  ==============================================================================
*/

#include <Audio/ExportMixImplementation.h>
#include <Audio/ExportMixToM3U.h>
#include <Audio/ExportMixToMp3.h>
#include <Audio/ExportMixToWav.h>
#include <Audio/MixExporter.h>
#include <Audio/MixRecoveryM3U.h>
#include <Database/Includes/Constants.h>
#include <Database/Includes/IMixManager.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <spdlog/fmt/chrono.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace audio
    {
        using namespace jucyaudio::database;

        ExportResult MixExporter::exportMixToFile(MixId mixId, const audio::ActiveExportSettings &settings, MixExporterProgressCallback progressCallback) const
        {
            const auto targetExtension{getLowercaseExtension(settings.outputPath)};

            // Handle M3U export separately (doesn't use ExportMixImplementation)
            if (targetExtension == ".m3u")
            {
                ExportMixToM3U m3uExporter{};
                bool success = m3uExporter.exportMix(mixId, settings.outputPath, progressCallback);
                return success ? ExportResult::Success() : ExportResult::Failure("M3U export failed");
            }

            // Handle audio format exports
            spdlog::info("MTE: Initializing export for mix {} -> {}", mixId, pathToString(settings.outputPath));

            ExportResult result;
            // Kept beyond the implementation's scope: this is the track list that was actually rendered,
            // and the recovery capture below refuses to record the mix if the live rows no longer match
            // it. The renderer reads the mix once at construction and then renders for minutes, so what
            // it used and what the database holds afterwards are not the same question.
            std::vector<MixTrack> renderedTracks;
            if (targetExtension == ".mp3")
            {
                ExportMp3MixImplementation implementation{mixId, settings, progressCallback};
                result = implementation.run();
                renderedTracks = implementation.getMixTracks();
            }
            else if (targetExtension == ".wav")
            {
                ExportWavMixImplementation implementation{mixId, settings, progressCallback};
                result = implementation.run();
                renderedTracks = implementation.getMixTracks();
            }
            else
            {
                spdlog::error("MTE: Unsupported output file extension: {}", targetExtension);
                if (progressCallback)
                    progressCallback(1.0f, "Error: Unsupported output format.");
                return ExportResult::Failure("Unsupported output format: " + targetExtension);
            }

            // Record what this mix contained, now that it has produced something worth identifying later.
            // Only on success: a record of a mix that failed to render describes nothing that exists.
            //
            // Deliberately not reached by the .m3u branch above, which returns early. Asking for a
            // playlist file is not finalising a mix, and it should neither write a recovery record nor
            // mark an editable mix exported.
            if (result.success)
            {
                const auto &mixManager = database::theTrackLibrary.getMixManager();
                database::MixRecoveryCapture capture;
                if (const auto captureResult = mixManager.captureRecoveryData(mixId, capture, &renderedTracks); !captureResult.isOk())
                {
                    result.recoveryWarning = "The mix was exported, but its recovery record could not be written: " + captureResult.errorMessage;
                    spdlog::error("MTE: {}", result.recoveryWarning);
                }
                else if (!capture.captured)
                {
                    result.recoveryWarning = "The mix was exported, but no recovery record was written: " + capture.skipReason;
                    spdlog::warn("MTE: {}", result.recoveryWarning);
                }
                else
                {
                    spdlog::info("MTE: Recorded recovery data for mix {} ({} tracks).", mixId, capture.entries.size());

                    // The companion file, written entirely from what the capture committed - rows and
                    // total duration both. Looking the duration up again here would be reading a second
                    // moment: another instance can edit the mix in between, and a deleted one would come
                    // back as zero.
                    const auto companionPath = companionM3UPathFor(settings.outputPath);
                    if (const auto m3uError = writeMixRecoveryM3U(companionPath, capture.entries, capture.totalDuration); !m3uError.empty())
                    {
                        result.recoveryWarning = "The mix was exported and recorded, but its companion playlist could not be written: " + m3uError;
                        spdlog::error("MTE: {}", result.recoveryWarning);
                    }
                }
            }

            // Mark the mix as exported if successful
            if (result.success && !settings.exportFolder.empty())
            {
                const auto& mixManager = database::theTrackLibrary.getMixManager();
                mixManager.setMixExported(mixId, settings.exportFolder);
                spdlog::info("Marked mix {} as Exported to folder '{}'", mixId, settings.exportFolder);
            }
            else if (result.success)
            {
                // Legacy: If no folder specified, just mark as exported
                const auto& mixManager = database::theTrackLibrary.getMixManager();
                mixManager.setMixStatus(mixId, "Exported");
                spdlog::info("Marked mix {} as Exported (no folder)", mixId);
            }

            return result;
        }
    } // namespace audio
} // namespace jucyaudio
