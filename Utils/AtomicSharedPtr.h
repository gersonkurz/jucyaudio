#pragma once

#include <atomic>
#include <memory>

namespace jucyaudio
{
    namespace util
    {
        /**
         * @brief Cross-platform atomic shared_ptr wrapper.
         *
         * On MSVC (C++20), uses std::atomic<std::shared_ptr<T>> which is the modern,
         * non-deprecated approach with superior performance characteristics.
         *
         * On other compilers (notably Apple's libc++ which lacks this specialization),
         * falls back to C++11 std::atomic_load/store/exchange free functions.
         *
         * This wrapper provides a consistent interface across platforms, isolating
         * the platform-specific code in one place.
         */
        template <typename T>
        class AtomicSharedPtr
        {
        public:
            AtomicSharedPtr() = default;

            // Non-copyable, non-movable to prevent data races.
            // Copying a plain shared_ptr while another thread stores would be a race.
            AtomicSharedPtr(const AtomicSharedPtr&) = delete;
            AtomicSharedPtr& operator=(const AtomicSharedPtr&) = delete;
            AtomicSharedPtr(AtomicSharedPtr&&) = delete;
            AtomicSharedPtr& operator=(AtomicSharedPtr&&) = delete;

            explicit AtomicSharedPtr(std::shared_ptr<T> ptr)
#if defined(_MSC_VER)
                : m_ptr{std::move(ptr)}
#else
                : m_ptr{std::move(ptr)}
#endif
            {
            }

            AtomicSharedPtr(std::nullptr_t)
#if defined(_MSC_VER)
                : m_ptr{nullptr}
#else
                : m_ptr{nullptr}
#endif
            {
            }

            std::shared_ptr<T> load() const
            {
#if defined(_MSC_VER)
                return m_ptr.load();
#else
                return std::atomic_load(&m_ptr);
#endif
            }

            void store(std::shared_ptr<T> desired)
            {
#if defined(_MSC_VER)
                m_ptr.store(std::move(desired));
#else
                std::atomic_store(&m_ptr, std::move(desired));
#endif
            }

            std::shared_ptr<T> exchange(std::shared_ptr<T> desired)
            {
#if defined(_MSC_VER)
                return m_ptr.exchange(std::move(desired));
#else
                return std::atomic_exchange(&m_ptr, std::move(desired));
#endif
            }

            explicit operator bool() const
            {
                return load() != nullptr;
            }

        private:
#if defined(_MSC_VER)
            std::atomic<std::shared_ptr<T>> m_ptr{nullptr};
#else
            std::shared_ptr<T> m_ptr{nullptr};
#endif
        };

    } // namespace util
} // namespace jucyaudio
