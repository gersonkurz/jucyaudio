#pragma once

#include <algorithm> // For std::isspace in C++ way, or use <cctype> for C way
#include <ranges>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <filesystem>
#include <Database/Includes/Constants.h>

namespace jucyaudio
{
    std::string timestampToString(const Timestamp_t &tp, const std::string &format = "%Y-%m-%d %H:%M");
    std::string durationToString(const Duration_t &d);
    
    inline int64_t timestampToInt64(const Timestamp_t &tp)
    {
        return std::chrono::duration_cast<Duration_t>(tp.time_since_epoch()).count();
    }

    inline Timestamp_t timestampFromInt64(int64_t millis)
    {
        if (millis == 0)
            return {}; // Return epoch time point

        return Timestamp_t{Duration_t{millis}};
    }

    inline int64_t durationToInt64(const Duration_t &ms)
    {
        return ms.count();
    }

    inline Duration_t durationFromInt64(int64_t value)
    {
        return Duration_t{value};
    }

    // not used in the database, but e.g. in TagLib:
    inline Duration_t durationFromIntSeconds(int32_t value)
    {
        // duration is time in milliseconds, so we multiply by 1000
        return Duration_t{value * 1000};
    }

    // Utility functions for sample conversion (to be implemented in .cpp)
    int64_t durationToSampleRate(Duration_t duration, double sampleRate);
    Duration_t durationFromSampleDate(int64_t samples, double sampleRate);

    // For convenience when working with JUCE (which often uses double seconds)
    double durationToDoubleSeconds(Duration_t duration);
    Duration_t durationFromDoubleSeconds(double seconds);

    inline std::u8string u8FromString(const std::string &str)
    {
        return std::u8string(reinterpret_cast<const char8_t *>(str.data()), str.size());
    }

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

    inline std::filesystem::path pathFromString(const std::string &str)
    {
        return std::filesystem::path{u8FromString(str)};
    }

    inline std::string pathToString(const std::filesystem::path &path)
    {
        return u8ToString(path.u8string());
    }

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

    // Removes leading and trailing whitespace from a std::string_view
    // and returns a new std::string_view.
    // Note: This returns a view into the original string. If you need an owned std::string, convert it.
    inline std::string_view trim_string_view(std::string_view s)
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

    // If you need an owned std::string from the trim:
    inline std::string trim_to_string(std::string_view s)
    {
        return std::string{trim_string_view(s)};
    }
    std::vector<std::string> split_string(std::string_view svtext, std::string_view svseparators, bool handle_quotation_marks = false);


} // namespace jucyaudio
