#include <Database/BackgroundService.h>
#include <spdlog/spdlog.h> // Assuming spdlog is a core dependency

namespace jucyaudio
{
    namespace database
    {
        BackgroundTaskService theBackgroundTaskService;
        
        BackgroundTaskService::~BackgroundTaskService()
        {
            // Ensure stop() is called if the user forgets.
            if (m_thread.joinable())
            {
                stop();
            }

            // Release our references to the tasks
            const std::lock_guard<std::mutex> lock(m_tasksMutex);
            for (auto *task : m_tasks)
                task->release(REFCOUNT_DEBUG_ARGS);
            m_tasks.clear();
        }

        void BackgroundTaskService::start()
        {
            if (!m_thread.joinable())
            {
                m_thread = std::thread(&BackgroundTaskService::run, this);
            }
        }

        void BackgroundTaskService::stop()
        {
            m_shouldExit = true;
            notify(); // Wake up the thread so it can see the exit flag
            if (m_thread.joinable())
            {
                m_thread.join();
            }
        }

        void BackgroundTaskService::registerTask(IBackgroundTask *task)
        {
            if (task)
            {
                const std::lock_guard<std::mutex> lock(m_tasksMutex);
                task->retain(REFCOUNT_DEBUG_ARGS);
                m_tasks.push_back(task);
                notify(); // Wake up to check the new task
            }
        }

        void BackgroundTaskService::notify()
        {
            m_condition.notify_one();
        }

        void BackgroundTaskService::pause()
        {
            m_isPaused = true;
            for(int i = 0; i < 10 && m_isProcessing; ++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Wait for current task to finish
            }
        }

        void BackgroundTaskService::resume()
        {
            m_isPaused = false;
            notify();
        }

        void BackgroundTaskService::run()
        {
            while (!m_shouldExit)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Simulate work
                
                // --- Wait for work or notification ---
                {
                    std::unique_lock<std::mutex> lock(m_conditionMutex);
                    // The condition variable waits until notify() is called OR the timeout elapses.
                    // We add a predicate to handle spurious wakeups.
                    m_condition.wait_for(lock, std::chrono::seconds(5),
                                         [this]
                                         {
                                             // Wake up if we are exiting or have been notified.
                                             return m_shouldExit.load() || !m_tasks.empty();
                                         });
                }

                if (m_shouldExit)
                    break;

                // If paused, just loop back and wait again.
                if (m_isPaused)
                    continue;

                // --- Round-robin through all registered tasks ---
                m_isProcessing = true;
                std::vector<IBackgroundTask *> tasksCopy;
                {
                    const std::lock_guard<std::mutex> lock(m_tasksMutex);
                    tasksCopy = m_tasks;
                }
                for (auto *task : tasksCopy)
                {
                    if (m_shouldExit || m_isPaused)
                        break;

                    try
                    {
                        task->processWork();
                        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Simulate work
                    }
                    catch (const std::exception &e)
                    {
                        spdlog::error("Task '{}' threw an exception: {}", task->m_taskName, e.what());
                    }
                }
                m_isProcessing = true;
            }
            spdlog::info("BackgroundTaskService thread finished.");
        }
    } // namespace database
} // namespace jucyaudio