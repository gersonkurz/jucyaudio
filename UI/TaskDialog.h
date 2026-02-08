#pragma once

#include <Database/Includes/ILongRunningTask.h> // Your updated, IRefCounted interface
#include <atomic>
#include <functional> // For std::function
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <thread>

namespace jucyaudio
{
    namespace ui
    {
        class TaskDialog : public juce::Component, public juce::Button::Listener, public juce::Timer
        {
        public:
            // Auto-close behavior options for clearer API
            enum class AutoCloseMode
            {
                NoAutoClose, // Dialog stays open, user must click Close
                Immediate,   // Close immediately on success (0ms delay)
                WithDelay    // Close after specified delay
            };

            // New constructor with clearer auto-close semantics
            TaskDialog(database::ILongRunningTask *task,
                std::function<void(bool success)> onCompletion = nullptr,
                AutoCloseMode closeMode = AutoCloseMode::NoAutoClose,
                int delayMs = 500); // Default delay when using WithDelay


            ~TaskDialog() override;

            void paint(juce::Graphics &g) override;
            void resized() override;

            void buttonClicked(juce::Button *button) override;
            void timerCallback() override;

            // Enhanced static launcher with clearer API
            static void launch(const juce::String &windowTitle,
                database::ILongRunningTask *taskToRun,
                AutoCloseMode closeMode = AutoCloseMode::NoAutoClose,
                int delayMs = 500,
                juce::Component *parentToCenterOn = nullptr,
                std::function<void(bool success)> onCompletion = nullptr);

        private:
            void startTask();
            void handleTaskCompleted(bool success, const std::string &resultMessage);
            void handleProgressUpdate(int progressPercent, const std::string &statusMessage);
            void closeDialog(int modalReturnValue);

            database::ILongRunningTask *m_task; // Retained pointer
            std::function<void(bool success)> m_onCompletion;
            std::optional<int> m_autoCloseOnSuccessDelayMs;
            bool m_waitingForAutoClose = false;
            juce::LookAndFeel_V4 m_lookAndFeel; // Custom LookAndFeel instance
            int m_lastProgressInPercent;
            std::string m_lastStatusMessage;

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
