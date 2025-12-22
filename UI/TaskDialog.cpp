#include "TaskDialog.h" // Assuming TaskDialog.h is in the same folder or path is set
#include "ILongRunningTask.h"
#include <UI/MainComponent.h>
#include <spdlog/spdlog.h> // For logging, if using spdlog

using namespace jucyaudio::database; // For easier access to database types

namespace jucyaudio
{
    namespace ui
    {

        namespace
        { // Anonymous namespace for constants
            const int TIMER_INTERVAL_MS = 100;
            const int DEFAULT_DIALOG_WIDTH = 900;
            const int DEFAULT_DIALOG_HEIGHT = 180;
        } // namespace

        // New constructor with AutoCloseMode for clearer API
        TaskDialog::TaskDialog(ILongRunningTask *task,
                               std::function<void()> onCompletion,
                               AutoCloseMode closeMode,
                               int delayMs)
            : m_task{task}, // Store raw pointer
              m_onCompletion{std::move(onCompletion)},
              // Convert AutoCloseMode to optional<int> for internal use
              m_autoCloseOnSuccessDelayMs{
                  closeMode == AutoCloseMode::NoAutoClose ? std::nullopt :
                  closeMode == AutoCloseMode::Immediate ? std::optional<int>(0) :
                  std::optional<int>(delayMs)
              },
              m_lastProgressInPercent{-1},
              m_lastStatusMessage{},
              // Use taskName from the task object for the title label
              m_titleLabel{"title", task ? juce::String(task->m_taskName) : "Processing Task"},
              m_statusLabel{"status", "Initializing..."},
              m_progressValue{0.0},
              m_progressBar{m_progressValue},
              // Use isCancellable from the task object for the button text
              m_actionButton{task && task->m_isCancellable ? "Cancel" : "Close"}
        {
            theThemeManager.applyCurrentTheme(m_lookAndFeel, this);

            if (m_task)
            {
                m_task->retain(REFCOUNT_DEBUG_ARGS);
            }
            else
            {
                jassertfalse; // Task should not be null
                // Fallback if task is null, though this is an error condition
                m_titleLabel.setText("Error: No Task", juce::dontSendNotification);
                m_actionButton.setButtonText("Close");
            }

            // UI Element Setup (addAndMakeVisible, fonts, justifications)
            addAndMakeVisible(m_titleLabel);
            m_titleLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(24.0f)}.boldened());
            m_titleLabel.setJustificationType(juce::Justification::left);

            addAndMakeVisible(m_statusLabel);
            m_statusLabel.setJustificationType(juce::Justification::centredLeft);
            m_statusLabel.setMinimumHorizontalScale(0.5f);

            addAndMakeVisible(m_progressBar);
            m_progressBar.setPercentageDisplay(false); // Default to indeterminate look

            addAndMakeVisible(m_actionButton);
            m_actionButton.addListener(this);
            m_actionButton.setWantsKeyboardFocus(true);

            setSize(DEFAULT_DIALOG_WIDTH, DEFAULT_DIALOG_HEIGHT);

            if (m_task)
            {
                startTask();
            }
            else
            {
                m_statusLabel.setText("Critical Error: Task object is null.", juce::dontSendNotification);
                m_progressBar.setVisible(false);
                m_actionButton.setEnabled(true); // Ensure "Close" button is clickable
            }
            startTimer(TIMER_INTERVAL_MS); // Timer is mainly for UI responsiveness checks or indeterminate animation
        }

        TaskDialog::~TaskDialog()
        {
            if (m_onCompletion)
            {
                m_onCompletion();
            }
            setLookAndFeel(nullptr);
            spdlog::info("TaskDialog destructor called for task: {}", m_task ? m_task->m_taskName : "UNKNOWN_OR_NULL");
            stopTimer(); // Stop any JUCE timers associated with this component

            // Signal cancellation to the running task if it's still considered running
            // from the UI's perspective OR if the thread is joinable (meaning it might still be finishing up).
            if (m_taskIsRunning.load() || m_taskThread.joinable())
            {
                m_shouldCancel = true;
                spdlog::info("TaskDialog: Signalled cancel to thread for task: {}", m_task ? m_task->m_taskName : "UNKNOWN_OR_NULL");
            }

            if (m_taskThread.joinable())
            {
                spdlog::info("TaskDialog: Attempting to join task thread for: {}", m_task ? m_task->m_taskName : "UNKNOWN_OR_NULL");
                try
                {
                    m_taskThread.join(); // Wait for the thread to complete
                    spdlog::info("TaskDialog: Task thread joined successfully for: {}", m_task ? m_task->m_taskName : "UNKNOWN_OR_NULL");
                }
                catch (const std::system_error &e)
                {
                    spdlog::error("TaskDialog: Exception while joining task thread: {}", e.what());
                    // Potentially detach if join fails and app needs to close, but this can be risky.
                    // If join throws, it often means the thread wasn't joinable (e.g., already finished and joined, or detached).
                }
            }
            else
            {
                spdlog::info("TaskDialog: Task thread was not joinable for: {}", m_task ? m_task->m_taskName : "UNKNOWN_OR_NULL");
            }

            if (m_task)
            {
                spdlog::info("TaskDialog: Releasing task: {}", m_task->m_taskName);
                m_task->release(REFCOUNT_DEBUG_ARGS);
                m_task = nullptr;
            }
            spdlog::info("TaskDialog destructor finished.");
        }

        void TaskDialog::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        }

        void TaskDialog::resized()
        {
            juce::FlexBox fb;
            fb.flexDirection = juce::FlexBox::Direction::column;
            fb.justifyContent = juce::FlexBox::JustifyContent::spaceBetween; // Pushes button to bottom
            fb.alignItems = juce::FlexBox::AlignItems::stretch;

            // Margins
            const float mainMargin = 10.0f;
            const float interElementMargin = 5.0f;

            fb.items.add(
                juce::FlexItem(m_titleLabel).withHeight(30.0f).withMargin(juce::FlexItem::Margin(mainMargin, mainMargin, interElementMargin, mainMargin)));
            fb.items.add(juce::FlexItem(m_statusLabel).withHeight(50.0f).withMargin(juce::FlexItem::Margin(0, mainMargin, interElementMargin, mainMargin)));
            fb.items.add(juce::FlexItem(m_progressBar)
                    .withHeight(20.0f)
                    .withMargin(juce::FlexItem::Margin(0, mainMargin, mainMargin, mainMargin))); // More margin below progress bar

            juce::FlexBox buttonBoxFb;
            buttonBoxFb.justifyContent = juce::FlexBox::JustifyContent::flexEnd; // Button to the right
            buttonBoxFb.items.add(juce::FlexItem(m_actionButton).withWidth(100.0f).withHeight(30.0f));

            fb.items.add(juce::FlexItem(buttonBoxFb).withHeight(30.0f).withMargin(juce::FlexItem::Margin(0, mainMargin, mainMargin, mainMargin)));

            fb.performLayout(getLocalBounds().reduced(static_cast<int>(mainMargin / 2)).toFloat()); // Small overall margin
        }

        void TaskDialog::startTask()
        {
            // ... (initial setup) ...
            m_taskIsRunning = true; // Set this before starting thread

            // Create SafePointer BEFORE starting thread - this must be done on the message thread
            juce::Component::SafePointer<TaskDialog> safeThis = this;

            m_taskThread = std::thread(
                [safeThis, taskPtr = m_task, shouldCancelPtr = &m_shouldCancel]
                {
                    bool cb_called_by_task = false;

                    ProgressCallback progressCbWrapper = [safeThis, currentTaskName = taskPtr->m_taskName](int progressPercent, const std::string &statusMsg)
                    {
                        std::string msgCopy(statusMsg);
                        juce::MessageManager::callAsync(
                            [safeThis, progressPercent, message = std::move(msgCopy), taskName = currentTaskName]()
                            {
                                // Check if dialog still exists before accessing any members
                                if (!safeThis)
                                    return;
                                // Check if task is still relevant
                                if (safeThis->m_task && safeThis->m_task->m_taskName == taskName && !safeThis->m_taskHasCompleted)
                                {
                                    safeThis->handleProgressUpdate(progressPercent, message);
                                }
                            });
                    };

                    CompletionCallback completionCbWrapper = [&cb_called_by_task, safeThis, currentTaskName = taskPtr->m_taskName](bool success, const std::string &resultMsg)
                    {
                        cb_called_by_task = true;
                        std::string msgCopy(resultMsg);
                        juce::MessageManager::callAsync(
                            [safeThis, success, message = std::move(msgCopy), taskName = currentTaskName]()
                            {
                                // Check if dialog still exists before accessing any members
                                if (!safeThis)
                                    return;
                                // Check if task is still relevant
                                if (safeThis->m_task && safeThis->m_task->m_taskName == taskName)
                                {
                                    safeThis->handleTaskCompleted(success, message);
                                }
                            });
                    };

                    try
                    {
                        if (taskPtr)
                        { // Ensure taskPtr is still valid
                            taskPtr->run(progressCbWrapper, completionCbWrapper, *shouldCancelPtr);
                        }
                    }
                    catch (const std::exception &e)
                    {
                        spdlog::error("TaskDialog: Exception in task '{}': {}", taskPtr ? taskPtr->m_taskName : "UNKNOWN", e.what());
                        if (!cb_called_by_task && taskPtr)
                        {
                            std::string errorMsg = "Task failed with exception: " + std::string(e.what());
                            juce::MessageManager::callAsync(
                                [safeThis, message = std::move(errorMsg), taskName = taskPtr->m_taskName]()
                                {
                                    if (safeThis && safeThis->m_task && safeThis->m_task->m_taskName == taskName)
                                        safeThis->handleTaskCompleted(false, message);
                                });
                        }
                        cb_called_by_task = true;
                    }
                    catch (...)
                    {
                        spdlog::error("TaskDialog: Unknown exception in task '{}'", taskPtr ? taskPtr->m_taskName : "UNKNOWN");
                        if (!cb_called_by_task && taskPtr)
                        {
                            std::string errorMsg = "Task failed with unknown exception.";
                            juce::MessageManager::callAsync(
                                [safeThis, message = std::move(errorMsg), taskName = taskPtr->m_taskName]()
                                {
                                    if (safeThis && safeThis->m_task && safeThis->m_task->m_taskName == taskName)
                                        safeThis->handleTaskCompleted(false, message);
                                });
                        }
                        cb_called_by_task = true;
                    }

                    // Fallback if task's run() finished but didn't call completionCb.
                    if (!cb_called_by_task && taskPtr)
                    {
                        spdlog::warn("TaskDialog: Task '{}' finished run() without calling completion callback. Triggering fallback.", taskPtr->m_taskName);

                        // Capture the CURRENT state of m_shouldCancel by value for this async call.
                        bool was_cancelled_at_this_point = shouldCancelPtr->load();

                        juce::MessageManager::callAsync(
                            [safeThis, cancelled_flag = was_cancelled_at_this_point, task_name_capture = taskPtr->m_taskName]()
                            {
                                // Check if dialog still exists before accessing any members
                                if (!safeThis)
                                    return;
                                if (safeThis->m_task && safeThis->m_task->m_taskName == task_name_capture && !safeThis->m_taskHasCompleted.load())
                                {
                                    std::string finalMsg = cancelled_flag ? "Task cancelled by user (fallback)." : "Task finished unexpectedly (fallback).";
                                    safeThis->handleTaskCompleted(false, finalMsg);
                                }
                            });
                    }

                    // Signal that the thread's main work is done. The UI thread will handle m_taskIsRunning.
                    // No more direct manipulation of 'this' members from here.
                });
        }

        void TaskDialog::handleProgressUpdate(int progressPercent, const std::string &statusMessage)
        {
            if ((m_lastProgressInPercent == progressPercent) && (m_lastStatusMessage == statusMessage))
                return;

            spdlog::info("TaskDialog: handleProgressUpdate called with progress {} and message '{}'", progressPercent, statusMessage);

            m_lastProgressInPercent = progressPercent;
            m_lastStatusMessage = statusMessage;

            if (m_taskHasCompleted)
                return;

            m_statusLabel.setText(juce::String::fromUTF8(statusMessage.data()), juce::dontSendNotification);
            if (progressPercent < 0)
            {
                if (m_isProgressBarDeterminate)
                {
                    m_progressValue = 0.0; // Reset progress for indeterminate state
                    m_isProgressBarDeterminate = false;
                    m_progressBar.setPercentageDisplay(false);
                }
            }
            else
            {
                m_progressValue = static_cast<double>(progressPercent) / 100.0;
                m_isProgressBarDeterminate = true;
                m_progressBar.setPercentageDisplay(true);
            }
        }

        void TaskDialog::handleTaskCompleted(bool success, const std::string &resultMessage)
        {
            spdlog::info("TaskDialog: handleTaskCompleted called with success {} and message '{}'", success, resultMessage);
            if (m_taskHasCompleted.exchange(true))
            {
                spdlog::warn("TaskDialog: handleTaskCompleted already processed, ignoring this call");
                return; // Already handled
            }
            m_taskIsRunning = false; // Mark task as no longer running from UI perspective too
            m_finalTaskSuccessState = success;

            m_statusLabel.setText(juce::String::fromUTF8(resultMessage.data()), juce::dontSendNotification);
            if (success)
            {
                m_progressValue = 1.0;             // Set to 100% on success
                m_isProgressBarDeterminate = true; // It's now 100%
            }
            else
            {
                // Keep current progress or set to 0 if it was indeterminate
                if (!m_isProgressBarDeterminate)
                    m_progressValue = 0.0;
            }
            spdlog::info("TaskDialog: Task completed, enabling action button");
            m_actionButton.setButtonText("Close");
            m_actionButton.setEnabled(true);
            m_actionButton.setVisible(true);
            m_actionButton.grabKeyboardFocus();

            if (success && m_autoCloseOnSuccessDelayMs.has_value())
            {
                m_waitingForAutoClose = true;
                if (*m_autoCloseOnSuccessDelayMs <= 0)
                {
                    closeDialog(1);
                }
                else
                {
                    // Use SafePointer
                    juce::Component::SafePointer<TaskDialog> self = this;
                    juce::Timer::callAfterDelay(*m_autoCloseOnSuccessDelayMs,
                        [self]
                        {
                            // Check if 'self' (the TaskDialog) still exists
                            if (self && self->m_waitingForAutoClose)
                            {
                                self->closeDialog(1);
                            }
                        });
                }
            }
        }

        void TaskDialog::timerCallback()
        {
            // Could be used for indeterminate progress bar animation
            // For example:
            if (m_taskIsRunning && !m_taskHasCompleted && !m_isProgressBarDeterminate)
            {
                m_progressValue += 0.1; // Adjust for desired speed of a "sweep"
                if (m_progressValue > 1.0)
                    m_progressValue = 0.0;
            }
        }

        void TaskDialog::buttonClicked(juce::Button *button)
        {
            if (button == &m_actionButton)
            {
                spdlog::info("TaskDialog: buttonClicked called");
                if (m_taskIsRunning.load() && !m_taskHasCompleted.load() && m_task && m_task->m_isCancellable)
                {
                    m_shouldCancel = true;
                    m_actionButton.setEnabled(false);
                    m_statusLabel.setText("Cancelling...", juce::dontSendNotification);
                }
                else
                {
                    m_waitingForAutoClose = false; // Prevent auto-close if user clicks "Close"
                    // Determine modal state based on whether task completed successfully
                    closeDialog(m_finalTaskSuccessState.load() ? 1 : 0);
                }
            }
        }

        void TaskDialog::closeDialog(int modalReturnValue)
        {
            spdlog::info("TaskDialog: closeDialog called with modalReturnValue {}", modalReturnValue);
            if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
            {
                spdlog::info("TaskDialog: calling exitModalState on DialogWindow");
                dw->exitModalState(modalReturnValue);
            }
            else
            {
                spdlog::info("TaskDialog: calling ...? nothing?");
                // If not in a DialogWindow (e.g., added directly to another component), just delete self
                // This assumes the dialog is always dynamically allocated and responsible for self-deletion
                // when used without DialogWindow::LaunchOptions.content.setOwned().
                // However, with launch using setOwned, DialogWindow handles deletion.
                // This else case is more for if you embed it directly.
                // For now, we assume it's in a DialogWindow.
            }
        }

        // New static launcher with AutoCloseMode
        void TaskDialog::launch(const juce::String &windowTitle,
                               ILongRunningTask *taskToRun,
                               AutoCloseMode closeMode,
                               int delayMs,
                               juce::Component *parentToCenterOn,
                               std::function<void()> onCompletion)
        {
            spdlog::info("TaskDialog::launch called with title '{}', taskToRun: {}, closeMode: {}, delayMs: {}, parent: {}",
                windowTitle.toStdString(),
                taskToRun ? taskToRun->m_taskName : "nullptr",
                static_cast<int>(closeMode),
                delayMs,
                parentToCenterOn ? juce::String(parentToCenterOn->getName()).toStdString() : "nullptr");

            if (!taskToRun)
            {
                spdlog::error("TaskDialog::launch: ERROR - taskToRun is nullptr.");
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Task Error", "Cannot start task: no task provided.");
                return;
            }

            auto *dialogComp = new TaskDialog{taskToRun, std::move(onCompletion), closeMode, delayMs};

            juce::DialogWindow::LaunchOptions optionsToUse;

            // Apply defaults and critical settings
            optionsToUse.content.setOwned(dialogComp);
            optionsToUse.dialogTitle = windowTitle.isNotEmpty() ? windowTitle : juce::String(dialogComp->m_task->m_taskName);
            if (parentToCenterOn)
            {
                optionsToUse.componentToCentreAround = parentToCenterOn;
            }
            optionsToUse.resizable = false; // Default for task dialogs

            // Default escape key behavior based on our dialog's button
            bool allowEscapeBasedOnButton = (dialogComp->m_actionButton.getButtonText() == "Close");
            optionsToUse.escapeKeyTriggersCloseButton = allowEscapeBasedOnButton;

            // Launch the dialog asynchronously
            optionsToUse.launchAsync();
        }

    } // namespace ui
} // namespace jucyaudio
