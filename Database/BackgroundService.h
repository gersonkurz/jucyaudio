#pragma once

#include <Database/Includes/IBackgroundTask.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace jucyaudio
{
    namespace database
    {
        class BackgroundTaskService final
        {
        public:
            BackgroundTaskService() = default;
            ~BackgroundTaskService();

            JUCE_DECLARE_NON_COPYABLE(BackgroundTaskService);

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
            std::mutex m_tasksMutex; // Protects m_tasks and used with m_condition

            std::condition_variable m_condition; // Replaces juce::WaitableEvent
            std::atomic<bool> m_isPaused{false};
            std::atomic<bool> m_isProcessing{false};
        };

        extern BackgroundTaskService theBackgroundTaskService;
    } // namespace database
} // namespace jucyaudio
