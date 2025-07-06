#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <Database/Includes/ILongRunningTask.h> // Your updated, IRefCounted interface
#include <atomic>
#include <functional> // For std::function
#include <optional>
#include <thread>

namespace jucyaudio
{
    namespace ui
    {
        class TaskDialog : public juce::Component, public juce::Button::Listener, public juce::Timer
        {
        public:
            // TaskDialog will retain the task. Caller should release if they also held a ref.
            TaskDialog(database::ILongRunningTask *task, // Takes a raw pointer, will retain
                       std::optional<int> autoCloseOnSuccessDelayMs = std::nullopt);
            ~TaskDialog() override;

            void paint(juce::Graphics &g) override;
            void resized() override;

            void buttonClicked(juce::Button *button) override;
            void timerCallback() override;

            // Static launcher method
            // The dialog will manage the lifetime of the task pointer via retain/release
            static void launch(const juce::String &windowTitle,
                               database::ILongRunningTask *taskToRun, // Caller ensures task exists, dialog retains
                               std::optional<int> autoCloseOnSuccessDelayMs = std::nullopt, juce::Component *parentToCenterOn = nullptr,
                               std::function<void(juce::DialogWindow::LaunchOptions &)> settingsCustomizer = nullptr);

        private:
            void startTask();
            void handleTaskCompleted(bool success, const std::string& resultMessage);
            void handleProgressUpdate(int progressPercent, const std::string& statusMessage);
            void closeDialog(int modalReturnValue);

            database::ILongRunningTask *m_task; // Retained pointer
            std::optional<int> m_autoCloseOnSuccessDelayMs;
            bool m_waitingForAutoClose = false;
            juce::LookAndFeel_V4 m_lookAndFeel; // Custom LookAndFeel instance

            // UI Elements
            juce::Label m_titleLabel;
            juce::Label m_statusLabel;
            double m_progressValue; // Used by ProgressBar
            juce::ProgressBar m_progressBar;
            juce::TextButton m_actionButton;

            // Task execution state
            std::atomic<bool> m_shouldCancel{false};
            std::atomic<bool> m_taskIsRunning{false};
            std::atomic<bool> m_taskHasCompleted{false};
            std::atomic<bool> m_finalTaskSuccessState{false};
            bool m_isProgressBarDeterminate{false};

            // Threading
            std::thread m_taskThread;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TaskDialog)
        };

    } // namespace ui
} // namespace jucyaudio
