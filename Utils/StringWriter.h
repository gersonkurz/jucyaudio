/*
 * This file is part of jucyaudio.
 * Copyright (C) 2025 Gerson Kurz <not@p-nand-q.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <format>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file StringWriter.h
 * @brief High-performance string building utility with small string optimization
 *
 * This header provides the StringWriter class which offers efficient string concatenation
 * by using a stack-allocated buffer for small strings and dynamically growing heap
 * allocation for larger strings. This approach minimizes memory allocations for
 * common use cases while supporting unlimited string growth.
 */

namespace jucyaudio
{
    /**
     * @brief A high-performance string builder with small string optimization
     *
     * StringWriter provides efficient string building capabilities by using a hybrid approach:
     * - Small strings (up to 1024 characters) use a stack-allocated buffer for maximum performance
     * - Larger strings automatically switch to dynamically allocated heap memory
     * - Dynamic buffer grows exponentially to minimize reallocations
     *
     * This class is particularly useful for building SQL queries, JSON strings, or any
     * scenario where multiple string concatenations are needed.
     *
     * @note The class is marked final to prevent inheritance and enable certain optimizations
     * @note All append operations return bool to indicate success/failure (memory allocation)
     */
    class StringWriter final
    {
    public:
        /**
         * @brief Default constructor - initializes with empty string using stack buffer
         * @note No memory allocation occurs during construction
         */
        StringWriter()
            : m_write_position{0},
              m_dynamic_size{0},
              m_dynamic_buffer{nullptr},
              m_builtin_buffer{}
        {
        }

        /**
         * @brief Copy constructor - creates a deep copy of the source StringWriter
         * @param source The StringWriter to copy from
         * @note May allocate memory if source uses dynamic buffer
         */
        StringWriter(const StringWriter &source)
            : m_write_position{0},
              m_dynamic_size{0},
              m_dynamic_buffer{nullptr},
              m_builtin_buffer{}
        {
            assign(source);
        }

        /**
         * @brief Copy assignment operator - performs deep copy assignment
         * @param source The StringWriter to copy from
         * @return Reference to this object
         * @note Handles self-assignment correctly
         * @note May allocate memory if source uses dynamic buffer
         */
        StringWriter &operator=(const StringWriter &source)
        {
            assign(source);
            return *this;
        }

        /**
         * @brief Move constructor - transfers ownership from source (noexcept)
         * @param source The StringWriter to move from (will be left in valid but empty state)
         * @note No memory allocation occurs, only ownership transfer
         * @note Source object will be reset to empty state
         */
        StringWriter(StringWriter &&source) noexcept;

        /**
         * @brief Move assignment operator - transfers ownership from source (noexcept)
         * @param source The StringWriter to move from (will be left in valid but empty state)
         * @return Reference to this object
         * @note Frees any existing dynamic memory before transfer
         * @note Source object will be reset to empty state
         */
        StringWriter &operator=(StringWriter &&source) noexcept;

        /**
         * @brief Destructor - automatically frees any allocated dynamic memory
         * @note Stack buffer requires no cleanup
         */
        ~StringWriter()
        {
            clear();
        }

        /**
         * @brief Checks if the string writer contains any characters
         * @return true if no characters have been written, false otherwise
         * @note This is a constant-time operation
         */
        bool empty() const
        {
            return (m_write_position == 0);
        }

        /**
         * @brief Converts the current content to a std::string
         * @return String containing all written characters
         * @note This operation temporarily appends a null terminator
         * @note The returned string is a copy, so it's safe to use after StringWriter destruction
         * @warning This method modifies internal state temporarily (mutable operation)
         */
        std::string asString() const
        {
            // this is needed to ensure that the string is zero-terminated
            const_cast<StringWriter *>(this)->append('\0');
            --m_write_position;
            return m_dynamic_buffer ? m_dynamic_buffer : m_builtin_buffer;
        }

        /**
         * @brief Convenience method to append a newline character
         * @return true on success, false if memory allocation failed
         * @note Equivalent to append('\n')
         */
        bool newline()
        {
            return append("\n");
        }

        /**
         * @brief Appends a single character to the string
         * @param c The character to append
         * @return true on success, false if memory allocation failed
         * @note May trigger transition from stack to heap buffer
         * @note Heap buffer grows exponentially when space is exhausted
         */
        bool append(char c);

        /**
         * @brief Appends a string view to the current content
         * @param s The string view to append
         * @return true on success, false if memory allocation failed
         * @note Handles empty/null string views safely
         * @note More efficient than character-by-character appending
         * @note May trigger buffer growth if needed
         */
        bool append(std::string_view s)
        {
            if (s.empty() || !s.data())
                return true;

            const auto len = s.size();
            char *wp = ensureFreeSpace(len);
            if (!wp)
                return false;

            memcpy(wp, s.data(), len);
            m_write_position += len;
            return true;
        }

        /**
         * @brief Appends formatted text using C++20 std::format
         * @tparam Args Variadic template parameter pack for format arguments
         * @param text Format string (must be compatible with std::format)
         * @param args Arguments to be formatted into the string
         * @return true on success, false if memory allocation failed
         * @note Uses C++20 std::format for type-safe formatting
         * @note Format string is checked at compile time when possible
         * @example writer.appendFormatted("User {} has {} items", name, count);
         */
        template <typename... Args> bool appendFormatted(const std::string_view text, Args &&...args)
        {
            return append(std::vformat(text, std::make_format_args(args...)));
        }

        /**
         * @brief Resets the string writer to empty state and frees dynamic memory
         * @note After calling clear(), the object returns to using the stack buffer
         * @note This is automatically called by the destructor
         */
        void clear()
        {
            if (m_dynamic_buffer)
            {
                free(m_dynamic_buffer);
                m_dynamic_buffer = nullptr;
                m_dynamic_size = 0;
            }
            m_write_position = 0;
        }

    private:
        /**
         * @brief Internal method to adjust write position after direct buffer writes
         * @param bytes_written Number of bytes that were written to the buffer
         * @note This method includes assertions to prevent buffer overruns
         * @note Used internally when external code writes directly to buffer
         * @warning For internal use only - improper use can cause buffer overruns
         */
        void correct(int bytes_written) const;

        /**
         * @brief Ensures sufficient free space is available and returns write pointer
         * @param space_needed Number of bytes of free space required
         * @return Pointer to location where writing can begin, or nullptr on allocation failure
         * @note May trigger transition from stack to heap buffer
         * @note May reallocate heap buffer if more space is needed
         * @note Buffer size grows exponentially to minimize future reallocations
         */
        char *ensureFreeSpace(size_t space_needed);

        /**
         * @brief Internal assignment implementation for copy operations
         * @param objectSrc The source StringWriter to copy from
         * @return true on success, false if memory allocation failed
         * @note Handles self-assignment correctly
         * @note Performs deep copy of either stack or heap buffer as appropriate
         */
        bool assign(const StringWriter &objectSrc);

    private:
        mutable size_t m_write_position; ///< Current write position (mutable for asString() const correctness)
        size_t m_dynamic_size;           ///< Size of dynamically allocated buffer (0 if using stack buffer)
        char *m_dynamic_buffer;          ///< Pointer to dynamic buffer (nullptr if using stack buffer)
        char m_builtin_buffer[1024];     ///< Stack-allocated buffer for small string optimization
    };

} // namespace jucyaudio