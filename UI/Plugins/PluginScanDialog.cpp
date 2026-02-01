#include <UI/Plugins/PluginScanDialog.h>

#include <Audio/Plugins/PluginManagerService.h>
#include <Database/Includes/ILongRunningTask.h>
#include <UI/Settings.h>
#include <UI/TaskDialog.h>
#include <format>

namespace jucyaudio
{
    namespace ui
    {
        namespace
        {
            juce::FileSearchPath getSearchPathFromSettings()
            {
                const auto configuredPaths = juce::String{config::theSettings.audioSettings.vst3ScanPaths};
                juce::FileSearchPath result;
                juce::StringArray lines;
                lines.addLines(configuredPaths);
                for (const auto &line : lines)
                {
                    const auto trimmed = line.trim();
                    if (trimmed.isNotEmpty())
                    {
                        result.add(trimmed);
                    }
                }

                if (result.getNumPaths() == 0)
                {
                    return audio::thePluginManagerService.getDefaultVst3SearchPath();
                }
                return result;
            }

            class PluginScanTask final : public database::ILongRunningTask
            {
            public:
                explicit PluginScanTask(juce::FileSearchPath searchPath)
                    : ILongRunningTask{"Scanning VST3 Plugins", true},
                      m_searchPath{std::move(searchPath)}
                {
                }

                void run(database::ProgressCallback progressCb,
                    database::CompletionCallback completionCb,
                    std::atomic<bool> &shouldCancel) override
                {
                    auto result = audio::thePluginManagerService.scanVst3Plugins(
                        m_searchPath,
                        true,
                        shouldCancel,
                        [&progressCb](int progressPercent, const std::string &statusMessage)
                        {
                            progressCb(progressPercent, statusMessage);
                        });

                    const bool cancelled = shouldCancel.load();
                    const std::string summary = std::format(
                        "{}. Scanned: {}. Added: {}. Failed: {}. Blacklisted: {}.",
                        cancelled ? "Scan cancelled" : "Scan complete",
                        result.scanned,
                        result.added,
                        result.failed,
                        result.blacklistedAdded);

                    completionCb(!cancelled, summary);
                }

            private:
                juce::FileSearchPath m_searchPath;
            };
        } // namespace

        void PluginScanDialog::launch(juce::Component *parentToCenterOn)
        {
            auto searchPath = getSearchPathFromSettings();
            auto *task = new PluginScanTask{std::move(searchPath)};
            TaskDialog::launch("Scan VST3 Plugins", task, TaskDialog::AutoCloseMode::NoAutoClose, 500, parentToCenterOn);
            task->release(REFCOUNT_DEBUG_ARGS);
        }
    } // namespace ui
} // namespace jucyaudio
