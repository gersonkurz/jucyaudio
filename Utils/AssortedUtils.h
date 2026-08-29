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
     * @brief Gets the lowercased, normalized file extension from a path.
     *
     * This function is Unicode-aware. It uses the platform's native Unicode APIs
     * to correctly handle extensions in any script or case.
     *
     * @param path The filesystem path to process.
     * @return A UTF-8 encoded, lowercased, and normalized string of the extension.
     *         Returns an empty string if there is no extension.
     */
    std::string getLowercaseExtension(const std::filesystem::path &path);

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

     /**
     * @brief Creates a canonical, normalized, case-folded key for a given string.
     *
     * This function is the core of the case-insensitive lookup strategy. It uses
     * platform-native APIs (Windows: NormalizeString/LCMapStringEx, macOS: CoreFoundation)
     * to perform Unicode-aware NFC normalization followed by case-folding. The resulting
     * string is suitable for use as a key in caches and for comparison.
     *
     * @param input A UTF-8 encoded string.
     * @return A UTF-8 encoded, normalized, case-folded string.
     */
    std::string normalizeForCache(std::string_view input);

    /**
     * @brief Case-folds a name the way SQLite's built-in NOCASE collation does, and no further.
     *
     * Deliberately not normalizeForCache(). That one is Unicode-aware, which is right for matching
     * filenames against a filesystem but wrong for deciding whether two rows in a table declared
     * UNIQUE COLLATE NOCASE are the same row. SQLite folds A-Z only: it will happily hold both
     * "Electro" and "electro" with an accented first letter as two separate rows, while a
     * Unicode-aware fold calls them one. Code that treats them as one then edits the wrong row's
     * data - matching albums to a vocabulary entry that did not own them.
     *
     * The result is a key: two names belong to the same NOCASE row exactly when their keys are
     * equal, so this works both as a comparison and as a map key. Byte length is unchanged, since
     * only ASCII letters are touched.
     *
     * @param input A UTF-8 encoded name.
     * @return The same bytes with A-Z lowered.
     */
    std::string noCaseKey(std::string_view input);

    /**
     * @brief Expands environment variables in a path string and normalizes separators.
     *
     * Recognizes ${VAR} syntax on all platforms.
     * Forward slashes are normalized to backslashes on Windows.
     *
     * @param path Path string potentially containing ${VAR} patterns
     * @return Expanded and normalized filesystem path
     *
     * @example expandPath("${LOCALAPPDATA}/jucyaudio/db.sqlite")
     *          -> "C:\\Users\\John\\AppData\\Local\\jucyaudio\\db.sqlite" (on Windows)
     */
    std::filesystem::path expandPath(const std::string& path);

    /**
     * @brief Gets the default configuration root directory for the current platform.
     *
     * If JUCYAUDIO_CONFIG environment variable is set, returns that path.
     * Otherwise returns platform-specific default:
     * - Windows: ${LOCALAPPDATA}/jucyaudio
     * - macOS: ${HOME}/Library/Application Support/jucyaudio
     * - Linux: ${HOME}/.config/jucyaudio
     *
     * @return The configuration root directory path (already expanded)
     */
    std::filesystem::path getConfigRoot();

    /**
     * @brief Gets the default configuration root path string (unexpanded) for the current platform.
     *
     * Returns the unexpanded path template with ${VAR} syntax.
     * - Windows: "${LOCALAPPDATA}/jucyaudio"
     * - macOS: "${HOME}/Library/Application Support/jucyaudio"
     * - Linux: "${HOME}/.config/jucyaudio"
     *
     * @return The default config root path template string
     */
    std::string getDefaultConfigRootTemplate();
} // namespace jucyaudio