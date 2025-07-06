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
                task->release();
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
                task->retain();
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
                // --- Wait for work or notification ---
                {
                    std::unique_lock<std::mutex> lock(m_conditionMutex);
                    // The condition variable waits until notify() is called OR the timeout elapses.
                    // We add a predicate to handle spurious wakeups.
                    m_condition.wait_for(lock, std::chrono::seconds(30),
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
                    }
                    catch (const std::exception &e)
                    {
                        spdlog::error("Task '{}' threw an exception: {}", task->getName(), e.what());
                    }
                }
            }
            spdlog::info("BackgroundTaskService thread finished.");
        }
    } // namespace database
} // namespace jucyaudio