#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#undef USE_REFCOUNT_DEBUGGING
#ifdef USE_REFCOUNT_DEBUGGING
#define REFCOUNT_DEBUG_ARGS __FILE__, __LINE__
#define REFCOUNT_DEBUG_SPEC const char *file, int line
#else
#define REFCOUNT_DEBUG_ARGS
#define REFCOUNT_DEBUG_SPEC
#endif

namespace jucyaudio
{
    namespace database
    {
        struct IRefCounted
        {
            IRefCounted() = default; // Default constructor
            virtual ~IRefCounted() = default;

            // Note: We use deleted move constructors/assignment operators to allow moving but prevent copying.
            IRefCounted(const IRefCounted &) = delete;            // No copy
            IRefCounted &operator=(const IRefCounted &) = delete; // No assignment
            IRefCounted(IRefCounted &&) = default;                // Move is fine
            IRefCounted &operator=(IRefCounted &&) = default;     // Move assignment is fine

            /// @brief Increment the reference count.
            /// This method should be called when a new reference to the object is created.
            /// It is expected to be thread-safe.
            virtual void retain(REFCOUNT_DEBUG_SPEC) const = 0;

            /// @brief Decrement the reference count.
            /// This method should be called when a reference to the object is no longer needed.
            /// If the reference count reaches zero, the object should be deleted.
            /// It is expected to be thread-safe.
            /// @note After calling this method, the object should not be used anymore.
            /// @note If the object is deleted, it should not call any methods on itself after this point.
            virtual void release(REFCOUNT_DEBUG_SPEC) const = 0;

            /// @brief Clear the internal state of the object.
            /// This method is intended to reset the internal state of the object without deleting it.
            /// It can be used to free resources or reset data. It does NOT touch the reference count.
            virtual void clear() = 0;
        };

        template <typename T> class RefCounted : public T
        {
        public:
            explicit RefCounted<T>()
                : m_refCount{1}
            {
            }

            void retain(REFCOUNT_DEBUG_SPEC) const override final
            {
                ++m_refCount;
            }
            void release(REFCOUNT_DEBUG_SPEC) const override final
            {
                if (--m_refCount == 0)
                    delete this;
            }
            void clear() override
            {
            } // Often unused in non-UI objects

        private:
            mutable std::atomic<int> m_refCount;
        };

        class RefCountImpl : public IRefCounted
        {
        public:
            RefCountImpl()
                : m_refCount{1}
            {
            }

            void retain(REFCOUNT_DEBUG_SPEC) const override final
            {
                ++m_refCount;
            }

            void release(REFCOUNT_DEBUG_SPEC) const override final
            {
                if (--m_refCount == 0)
                    delete this;
            }

            void clear() override
            {
            } // Often unused in non-UI objects

        private:
            mutable std::atomic<int> m_refCount;
        };
    } // namespace database
} // namespace jucyaudio
