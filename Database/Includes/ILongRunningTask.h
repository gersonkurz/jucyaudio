
// ILongRunningTask.h
#pragma once

#include <atomic>
#include <cassert>
#include <functional>
#include <string>      // For std::string if needed for storage by implementers
#include <string_view> // For std::string_view

#include <Database/Includes/Constants.h>
#include <Database/Includes/INavigationNode.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        // Callbacks provided TO the task by the dialog
        using ProgressCallback = std::function<void(int progressPercent, const std::string& statusMessage)>; // progress: 0-100, or -1 for indeterminate
        using CompletionCallback = std::function<void(bool success, const std::string& resultMessage)>;

        class ILongRunningTask : public IRefCounted
        {
        public:

            // --- Task Characteristics (public const members) ---
            const std::string m_taskName; // Task name for display
            const bool m_isCancellable;   // Can this task be cancelled?

            virtual ~ILongRunningTask()
            {
                assert(m_refCount.load() == 0); // Ensure no leaks
            }

            // Called on a background thread to perform the task.
            // The task signals completion (success/failure) and final message via completionCb.
            virtual void run(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel) = 0;

            // IRefCounted interface
            void retain(REFCOUNT_DEBUG_SPEC) const override final
            {
#ifdef USE_REFCOUNT_DEBUGGING
                const auto value = ++m_refCount;
                spdlog::debug("BaseNode::retain {:p} at {}[{}]: is now {}", (void *)this, file, line, value);
#else
                ++m_refCount;
#endif
            }

            void release(REFCOUNT_DEBUG_SPEC) const override final
            {
#ifdef USE_REFCOUNT_DEBUGGING
                const auto value = --m_refCount;
                if (value == 0)
                {
                    spdlog::warn("BaseNode::release {:p} at {}[{}]: is now {} <- delete this", (void *)this, file, line, value);
                    delete this;
                }
                else
                {
                    spdlog::debug("BaseNode::release {:p} at {}[{}]: is now {}", (void *)this, file, line, value);
                }
#else
                if (--m_refCount == 0)
                {
                    delete this;
                }
#endif
            }

            void clear() override
            {
            }

        protected:
            // Constructor for derived classes to set the const members
            ILongRunningTask(std::string_view name, bool cancellable)
                : m_taskName{name},
                  m_isCancellable{cancellable},
                  m_refCount{1} // Start with refcount 1
            {
#ifdef USE_REFCOUNT_DEBUGGING
                spdlog::debug("BaseNode::initialize {:p} at {}[{}]: is now {}", (void *)this, std::string{__FILE__}, __LINE__, (int32_t)m_refCount);
#endif
            }

        private:
            mutable std::atomic<int32_t> m_refCount; // Reference count for IRefCounted
        };
    } // namespace database
} // namespace jucyaudio
