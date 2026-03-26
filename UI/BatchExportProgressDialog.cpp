#include <UI/BatchExportProgressDialog.h>

#include <Database/TrackLibrary.h>
#include <UI/ThemeManager.h>
#include <format>
#include <numeric>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        namespace
        {
            constexpr int kDialogWidth = 900;
            constexpr int kDialogHeight = 240;

            void setProgressValue(double& target, int percent)
            {
                target = juce::jlimit(0.0, 1.0, static_cast<double>(percent) / 100.0);
            }
        } // namespace

        BatchExportProgressDialog::BatchExportProgressDialog(std::vector<database::IMixManager::ScheduledExport> mixes,
                                                             const audio::IMixExporter& exporter,
                                                             std::function<void(bool success)> onCompletion)
            : m_mixes{std::move(mixes)},
              m_exporter{exporter},
              m_onCompletion{std::move(onCompletion)},
              m_titleLabel{"title", "Batch Export"},
              m_overallStatusLabel{"overallStatus", "Preparing batch export..."},
              m_currentStatusLabel{"currentStatus", "Waiting to start..."},
              m_resultLabel{"result", ""},
              m_overallProgressBar{m_overallProgressValue},
              m_currentProgressBar{m_currentProgressValue},
              m_actionButton{"Cancel"}
        {
            theThemeManager.applyCurrentTheme(m_lookAndFeel, this);

            addAndMakeVisible(m_titleLabel);
            m_titleLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(24.0f)}.boldened());
            m_titleLabel.setJustificationType(juce::Justification::left);

            addAndMakeVisible(m_overallStatusLabel);
            m_overallStatusLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(16.0f)}.boldened());
            m_overallStatusLabel.setJustificationType(juce::Justification::centredLeft);

            addAndMakeVisible(m_currentStatusLabel);
            m_currentStatusLabel.setJustificationType(juce::Justification::centredLeft);
            m_currentStatusLabel.setMinimumHorizontalScale(0.5f);

            addAndMakeVisible(m_resultLabel);
            m_resultLabel.setJustificationType(juce::Justification::centredLeft);
            m_resultLabel.setMinimumHorizontalScale(0.5f);
            m_resultLabel.setVisible(false);

            addAndMakeVisible(m_overallProgressBar);
            m_overallProgressBar.setPercentageDisplay(false);

            addAndMakeVisible(m_currentProgressBar);
            m_currentProgressBar.setPercentageDisplay(false);

            addAndMakeVisible(m_actionButton);
            m_actionButton.addListener(this);
            m_actionButton.setWantsKeyboardFocus(true);

            setSize(kDialogWidth, kDialogHeight);
            startTask();
        }

        BatchExportProgressDialog::~BatchExportProgressDialog()
        {
            setLookAndFeel(nullptr);

            if (m_taskIsRunning.load() || m_taskThread.joinable())
            {
                m_shouldCancel = true;
            }

            if (m_taskThread.joinable())
            {
                try
                {
                    m_taskThread.join();
                }
                catch (const std::system_error& e)
                {
                    spdlog::error("BatchExportProgressDialog: join failed: {}", e.what());
                }
            }
        }

        void BatchExportProgressDialog::paint(juce::Graphics& g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        }

        void BatchExportProgressDialog::resized()
        {
            juce::FlexBox fb;
            fb.flexDirection = juce::FlexBox::Direction::column;
            fb.alignItems = juce::FlexBox::AlignItems::stretch;

            const float mainMargin = 10.0f;
            const float interMargin = 6.0f;

            fb.items.add(juce::FlexItem(m_titleLabel).withHeight(30.0f).withMargin({mainMargin, mainMargin, interMargin, mainMargin}));
            fb.items.add(juce::FlexItem(m_overallStatusLabel).withHeight(24.0f).withMargin({0, mainMargin, interMargin, mainMargin}));
            fb.items.add(juce::FlexItem(m_overallProgressBar).withHeight(20.0f).withMargin({0, mainMargin, interMargin, mainMargin}));
            fb.items.add(juce::FlexItem(m_currentStatusLabel).withHeight(40.0f).withMargin({0, mainMargin, interMargin, mainMargin}));
            fb.items.add(juce::FlexItem(m_currentProgressBar).withHeight(20.0f).withMargin({0, mainMargin, interMargin, mainMargin}));
            fb.items.add(juce::FlexItem(m_resultLabel).withHeight(26.0f).withMargin({0, mainMargin, interMargin, mainMargin}));

            juce::FlexBox buttonBox;
            buttonBox.justifyContent = juce::FlexBox::JustifyContent::flexEnd;
            buttonBox.items.add(juce::FlexItem(m_actionButton).withWidth(100.0f).withHeight(30.0f));
            fb.items.add(juce::FlexItem(buttonBox).withHeight(30.0f).withMargin({0, mainMargin, mainMargin, mainMargin}));

            fb.performLayout(getLocalBounds().reduced(5).toFloat());
        }

        void BatchExportProgressDialog::buttonClicked(juce::Button* button)
        {
            if (button != &m_actionButton)
                return;

            if (m_taskHasCompleted.load())
            {
                closeDialog(m_finalTaskSuccessState.load() ? 1 : 0);
                return;
            }

            if (m_taskIsRunning.load())
            {
                m_shouldCancel = true;
                m_actionButton.setEnabled(false);
                m_actionButton.setButtonText("Cancelling...");
            }
        }

        void BatchExportProgressDialog::startTask()
        {
            m_taskIsRunning = true;
            juce::Component::SafePointer<BatchExportProgressDialog> safeThis = this;

            m_taskThread = std::thread(
                [safeThis, mixes = m_mixes, &exporter = m_exporter, shouldCancel = &m_shouldCancel]()
                {
                    int successCount = 0;
                    int failCount = 0;

                    auto postProgress = [safeThis](int overallPercent,
                                                   std::string overallMessage,
                                                   int currentPercent,
                                                   std::string currentMessage)
                    {
                        juce::MessageManager::callAsync(
                            [safeThis,
                             overallPercent,
                             overallMessage = std::move(overallMessage),
                             currentPercent,
                             currentMessage = std::move(currentMessage)]() mutable
                            {
                                if (safeThis)
                                {
                                    safeThis->handleProgressUpdate(overallPercent,
                                                                   std::move(overallMessage),
                                                                   currentPercent,
                                                                   std::move(currentMessage));
                                }
                            });
                    };

                    auto postCompletion = [safeThis](bool success, std::string message)
                    {
                        juce::MessageManager::callAsync(
                            [safeThis, success, message = std::move(message)]() mutable
                            {
                                if (safeThis)
                                {
                                    safeThis->handleTaskCompleted(success, std::move(message));
                                }
                            });
                    };

                    const auto totalMixes = mixes.size();
                    if (totalMixes == 0)
                    {
                        postCompletion(false, "No mixes selected for export.");
                        return;
                    }

                    const auto mixWeight = [](const database::MixInfo& mixInfo) -> int64_t
                    {
                        return mixInfo.numberOfTracks > 0 ? mixInfo.numberOfTracks : int64_t{1};
                    };

                    const auto totalWeight = std::accumulate(
                        mixes.begin(),
                        mixes.end(),
                        int64_t{0},
                        [&mixWeight](int64_t sum, const database::IMixManager::ScheduledExport& scheduled)
                        {
                            return sum + mixWeight(scheduled.mixInfo);
                        });

                    int64_t completedWeight = 0;

                    for (size_t i = 0; i < totalMixes; ++i)
                    {
                        if (shouldCancel->load())
                        {
                            postCompletion(false, std::format("Cancelled after {}/{} mixes.", successCount, totalMixes));
                            return;
                        }

                        const auto& entry = mixes[i];
                        const auto& mixInfo = entry.mixInfo;
                        const auto& settings = entry.settings;
                        const auto currentMixWeight = mixWeight(mixInfo);

                        postProgress(static_cast<int>((completedWeight * 100) / totalWeight),
                                     std::format("Mix {} of {}: {}", i + 1, totalMixes, mixInfo.name),
                                     0,
                                     std::format("Finalizing '{}'...", mixInfo.name));

                        if (!database::theTrackLibrary.getMixManager().finalizeMix(mixInfo.mixId))
                        {
                            ++failCount;
                            spdlog::error("Batch export: failed to finalize mix '{}'", mixInfo.name);
                            completedWeight += currentMixWeight;
                            postProgress(static_cast<int>((completedWeight * 100) / totalWeight),
                                         std::format("Mix {} of {}: {}", i + 1, totalMixes, mixInfo.name),
                                         0,
                                         std::format("Failed to finalize '{}'", mixInfo.name));
                            continue;
                        }

                        const auto exportResult = exporter.exportMixToFile(
                            mixInfo.mixId,
                            settings,
                            [&](float progress, const std::string&)
                            {
                                const auto clamped = juce::jlimit(0.0f, 1.0f, progress);
                                const auto currentPercent = static_cast<int>(clamped * 100.0f);
                                const auto weightedProgress = static_cast<double>(completedWeight) + (static_cast<double>(clamped) * static_cast<double>(currentMixWeight));
                                const auto overallPercent = static_cast<int>((weightedProgress / static_cast<double>(totalWeight)) * 100.0);

                                postProgress(overallPercent,
                                             std::format("Mix {} of {}: {}", i + 1, totalMixes, mixInfo.name),
                                             currentPercent,
                                             std::format("Exporting '{}'... {}%", mixInfo.name, currentPercent));
                                return !shouldCancel->load();
                            });

                        if (shouldCancel->load())
                        {
                            postCompletion(false, std::format("Cancelled after {}/{} mixes.", successCount, totalMixes));
                            return;
                        }

                        if (exportResult.success)
                        {
                            database::theTrackLibrary.getMixManager().setMixExported(mixInfo.mixId, settings.exportFolder);
                            database::theTrackLibrary.getMixManager().clearPendingExportSettings(mixInfo.mixId);
                            ++successCount;
                            spdlog::info("Batch export: successfully exported mix '{}'", mixInfo.name);
                        }
                        else
                        {
                            ++failCount;
                            spdlog::error("Batch export: failed to export mix '{}': {}", mixInfo.name, exportResult.message);
                        }

                        completedWeight += currentMixWeight;
                    }

                    if (failCount == 0)
                    {
                        postCompletion(true, std::format("Successfully exported all {} mixes.", successCount));
                    }
                    else
                    {
                        postCompletion(false, std::format("Exported {}/{} mixes ({} failed).", successCount, totalMixes, failCount));
                    }
                });
        }

        void BatchExportProgressDialog::handleProgressUpdate(int overallPercent,
                                                             std::string overallMessage,
                                                             int currentPercent,
                                                             std::string currentMessage)
        {
            setProgressValue(m_overallProgressValue, overallPercent);
            setProgressValue(m_currentProgressValue, currentPercent);
            m_overallStatusLabel.setText(juce::String::fromUTF8(overallMessage.data()), juce::dontSendNotification);
            m_currentStatusLabel.setText(juce::String::fromUTF8(currentMessage.data()), juce::dontSendNotification);
        }

        void BatchExportProgressDialog::handleTaskCompleted(bool success, std::string resultMessage)
        {
            if (m_taskHasCompleted.exchange(true))
                return;

            m_taskIsRunning = false;
            m_finalTaskSuccessState = success;
            m_resultLabel.setText(juce::String::fromUTF8(resultMessage.data()), juce::dontSendNotification);
            m_resultLabel.setVisible(true);
            m_actionButton.setEnabled(true);
            m_actionButton.setButtonText("Close");

            if (success)
            {
                m_currentProgressValue = 1.0;
                m_overallProgressValue = 1.0;
            }

            if (m_onCompletion)
            {
                try
                {
                    m_onCompletion(success);
                }
                catch (const std::exception& e)
                {
                    spdlog::error("BatchExportProgressDialog: onCompletion threw: {}", e.what());
                }
            }
        }

        void BatchExportProgressDialog::closeDialog(int modalReturnValue)
        {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            {
                dw->exitModalState(modalReturnValue);
            }
        }

        void BatchExportProgressDialog::launch(std::vector<database::IMixManager::ScheduledExport> mixes,
                                               const audio::IMixExporter& exporter,
                                               juce::Component* parentToCenterOn,
                                               std::function<void(bool success)> onCompletion)
        {
            auto* dialogComp = new BatchExportProgressDialog(std::move(mixes), exporter, std::move(onCompletion));

            juce::DialogWindow::LaunchOptions launchOptions;
            launchOptions.content.setOwned(dialogComp);
            launchOptions.dialogTitle = "Batch Export";
            launchOptions.componentToCentreAround = parentToCenterOn;
            launchOptions.escapeKeyTriggersCloseButton = false;
            launchOptions.resizable = false;
            launchOptions.useNativeTitleBar = true;
            launchOptions.launchAsync();
        }

    } // namespace ui
} // namespace jucyaudio
