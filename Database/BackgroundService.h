#pragma once

#include <Database/Includes/IBackgroundTask.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        class BackgroundTaskService final
        {
        public:
            BackgroundTaskService() = default;
            ~BackgroundTaskService();

            // Deleted copy/move constructors to enforce singleton-like behavior
            BackgroundTaskService(const BackgroundTaskService &) = delete;
            BackgroundTaskService &operator=(const BackgroundTaskService &) = delete;

            void start();
            void stop();

            void registerTask(IBackgroundTask *task); // Task is retained

            // Wakes up the thread if it's sleeping.
            void notify();

            // Pauses execution AFTER the current task is finished.
            void pause();
            // Resumes execution.
            void resume();

        private:
            // The main thread loop function.
            void run();

            std::thread m_thread;
            std::atomic<bool> m_shouldExit{false};

            std::vector<IBackgroundTask *> m_tasks;
            std::mutex m_tasksMutex; // Replaces juce::CriticalSection

            std::condition_variable m_condition; // Replaces juce::WaitableEvent
            std::mutex m_conditionMutex;
            std::atomic<bool> m_isPaused{false};
            std::atomic<bool> m_isProcessing{false};
        };

        extern BackgroundTaskService theBackgroundTaskService;
    } // namespace database
} // namespace jucyaudio
