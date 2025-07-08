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
 */                                                                                                                                                            \
#pragma once

#include <Database/Includes/Constants.h>
#include <algorithm> // For std::isspace in C++ way, or use <cctype> for C way
#include <chrono>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file AssortedUtils.h
 * @brief Utility functions for common operations in the jucyaudio application
 *
 * This header provides various utility functions for string manipulation,
 * time/duration conversions, filesystem operations, and data type conversions.
 */

namespace jucyaudio
{
    /**
     * @brief Converts a timestamp to a formatted string representation
     * @param tp The timestamp to convert
     * @param format The format string (default: "%Y-%m-%d %H:%M")
     * @return Formatted string representation of the timestamp, or "Never" if timestamp is default-initialized
     */
    std::string timestampToString(const Timestamp_t &tp, const std::string &format = "%Y-%m-%d %H:%M");

    /**#include <sstream> // For std::ostringstream

     * @brief Converts a duration to a human-readable string representation
     * @param d The duration to convert
     * @return String in format "HH:MM:SS,mmm" (hours:minutes:seconds,milliseconds)
     */
    std::string durationToString(const Duration_t &d);

    /**
     * @brief Converts a timestamp to int64_t representation for database storage
     * @param tp The timestamp to convert
     * @return The timestamp as milliseconds since epoch
     */
    inline int64_t timestampToInt64(const Timestamp_t &tp)
    {
        return std::chrono::duration_cast<Duration_t>(tp.time_since_epoch()).count();
    }

    /**
     * @brief Converts an int64_t value back to a timestamp
     * @param millis Milliseconds since epoch
     * @return Timestamp object, or epoch time point if millis is 0
     */
    inline Timestamp_t timestampFromInt64(int64_t millis)
    {
        if (millis == 0)
            return {}; // Return epoch time point

        return Timestamp_t{Duration_t{millis}};
    }

    /**
     * @brief Converts a duration to int64_t representation for database storage
     * @param ms The duration to convert
     * @return The duration in milliseconds as int64_t
     */
    inline int64_t durationToInt64(const Duration_t &ms)
    {
        return ms.count();
    }

    /**
     * @brief Converts an int64_t value back to a duration
     * @param value The milliseconds value
     * @return Duration object representing the specified milliseconds
     */
    inline Duration_t durationFromInt64(int64_t value)
    {
        return Duration_t{value};
    }

    /**
     * @brief Converts seconds (as int32_t) to duration in milliseconds
     * @param value Duration in seconds
     * @return Duration object representing the specified seconds converted to milliseconds
     * @note Used for external libraries like TagLib that provide duration in seconds
     */
    inline Duration_t durationFromIntSeconds(int32_t value)
    {
        // duration is time in milliseconds, so we multiply by 1000
        return Duration_t{value * 1000};
    }

    /**
     * @brief Converts a std::string to std::u8string
     * @param str The string to convert
     * @return UTF-8 string representation
     * @note Assumes input string contains valid UTF-8 data
     */
    inline std::u8string u8FromString(const std::string &str)
    {
        return std::u8string(reinterpret_cast<const char8_t *>(str.data()), str.size());
    }

    /**
     * @brief Converts a std::u8string to std::string
     * @param u8s The UTF-8 string to convert
     * @return Standard string representation
     * @note This assumes the environment can handle UTF-8 bytes in std::string
     */
    inline std::string u8ToString(const std::u8string &u8s)
    {
        // This assumes that std::string can correctly hold and represent UTF-8 bytes,
        // which is true if your environment and terminal can handle it.
        // The reinterpret_cast is generally safe if char8_t and char have same size and
        // you are careful about encodings.
        if (u8s.empty())
            return "";
        return std::string{reinterpret_cast<const char *>(u8s.data()), u8s.length()};
    }

    /**
     * @brief Converts a string to a filesystem path
     * @param str The string representation of the path
     * @return std::filesystem::path object
     * @note Handles UTF-8 encoding properly for cross-platform compatibility
     */
    inline std::filesystem::path pathFromString(const std::string &str)
    {
        return std::filesystem::path{u8FromString(str)};
    }

    /**
     * @brief Converts a filesystem path to string representation
     * @param path The filesystem path to convert
     * @return String representation of the path with proper UTF-8 encoding
     */
    inline std::string pathToString(const std::filesystem::path &path)
    {
        return u8ToString(path.u8string());
    }

    /**
     * @brief Extracts the file extension in lowercase
     * @param path The filesystem path to extract extension from
     * @return Lowercase file extension (including the dot), or empty string if no extension
     */
    inline std::string getLowercaseExtension(const std::filesystem::path &path)
    {
        auto targetExtension{pathToString(path.extension())};
        std::transform(targetExtension.begin(), targetExtension.end(), targetExtension.begin(),
                       [](unsigned char c)
                       {
                           return static_cast<char>(std::tolower(c));
                       });
        return targetExtension;
    }

    /**
     * @brief Removes leading and trailing whitespace from a string view
     * @param s The string view to trim
     * @return A trimmed string view pointing to the original string data
     * @note Returns a view into the original string. Convert to std::string if owned copy is needed
     * @warning The returned string_view is only valid as long as the original string exists
     */
    inline std::string_view trimStringView(std::string_view s)
    {
        // Lambda to check for whitespace
        auto is_not_space = [](unsigned char ch)
        {
            return !std::isspace(ch);
        };

        // Find the first non-whitespace character
        auto first = std::find_if(s.begin(), s.end(), is_not_space);
        if (first == s.end())
        { // String is all whitespace or empty
            return {};
        }

        // Find the last non-whitespace character
        auto last = std::find_if(s.rbegin(), s.rend(), is_not_space).base();
        // .base() converts reverse_iterator to forward_iterator pointing one past the element

        return {&*first, static_cast<std::string_view::size_type>(std::distance(first, last))};
    }

    /**
     * @brief Trims whitespace and returns an owned string copy
     * @param s The string view to trim
     * @return A new std::string with leading and trailing whitespace removed
     */
    inline std::string trimToString(std::string_view s)
    {
        return std::string{trimStringView(s)};
    }

    /**
     * @brief Splits a string into tokens using specified separators
     * @param svtext The string to split
     * @param svseparators Characters to use as separators
     * @param handle_quotation_marks If true, treats quoted sections as single tokens
     * @return Vector of string tokens
     * @note When handle_quotation_marks is true, quoted strings are treated as single tokens
     *       regardless of separators within the quotes
     */
    std::vector<std::string> splitString(std::string_view svtext, std::string_view svseparators, bool handle_quotation_marks = false);

} // namespace jucyaudio