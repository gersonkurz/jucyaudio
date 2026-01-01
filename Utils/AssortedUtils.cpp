#include <Database/Includes/Constants.h>
#include <Utils/AssortedUtils.h>
#include <cassert>
#include <cstdlib>
#include <ranges>
#include <regex>
#include <spdlog/spdlog.h>

// Platform-specific headers for Unicode normalization and case folding
#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace jucyaudio
{

    std::string durationToString(const Duration_t &d)
    {
        using namespace std::chrono;

        auto total_millis = d.count();

        auto hours = total_millis / (1000 * 60 * 60);
        total_millis %= (1000 * 60 * 60);

        auto minutes = total_millis / (1000 * 60);
        total_millis %= (1000 * 60);

        auto seconds = total_millis / 1000;
        auto millis = total_millis % 1000;

        return std::format("{:02}:{:02}:{:02},{:03}", hours, minutes, seconds, millis);
    }

    std::string timestampToString(const Timestamp_t &tp, const std::string &format)
    {
        if (tp == Timestamp_t{})
        { // Check for default/uninitialized
            return "Never";
        }
        std::time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_struct{};
// Use localtime_s on Windows, localtime_r on POSIX for thread-safety
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&tm_struct, &t);
#else
        localtime_r(&t, &tm_struct);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm_struct, format.c_str());
        return oss.str();
    }

    std::vector<std::string> splitString(std::string_view svtext, std::string_view svseparators, bool handle_quotation_marks)
    {
        assert(!svseparators.empty());
        std::vector<std::string> result;

        if (svtext.empty())
        {
            return result;
        }

        bool is_recording_quoted_string = false;
        size_t start = 0;

        for (size_t i = 0; i < svtext.size(); ++i)
        {
            const char c = svtext[i];

            if (is_recording_quoted_string)
            {
                if (c == '"')
                {
                    // End of quoted string - don't include the closing quote
                    result.emplace_back(std::string{svtext.substr(start, i - start)});
                    start = i + 1;
                    is_recording_quoted_string = false;
                }
                continue;
            }

            if (handle_quotation_marks && c == '"')
            {
                // Start of quoted string - save any preceding content
                if (i > start)
                {
                    result.emplace_back(std::string{svtext.substr(start, i - start)});
                }
                start = i + 1;
                is_recording_quoted_string = true;
                continue;
            }

            // Check if c is a separator (using find instead of strchr for safety)
            if (svseparators.find(c) != std::string_view::npos)
            {
                result.emplace_back(std::string{svtext.substr(start, i - start)});
                start = i + 1;
            }
        }

        // Add remaining content after last separator
        if (start < svtext.size())
        {
            result.emplace_back(std::string{svtext.substr(start)});
        }

        return result;
    }

#if defined(_WIN32) || defined(_WIN64)
    // Windows implementation using NormalizeString and LCMapStringEx
    std::string normalizeForCache(std::string_view input)
    {
        if (input.empty())
        {
            return std::string{input};
        }

        // --- Step 1: Convert UTF-8 to UTF-16 (Windows uses wide strings) ---
        int utf16_len = MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
        if (utf16_len == 0)
        {
            spdlog::error("normalizeForCache: UTF-8 to UTF-16 conversion failed for input '{}'. Error: {}", input, GetLastError());
            return std::string{input};
        }

        std::wstring utf16_buffer(utf16_len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), utf16_buffer.data(), utf16_len);

        // --- Step 2: NFC Normalization ---
        int normalized_len = NormalizeString(NormalizationC, utf16_buffer.data(), utf16_len, nullptr, 0);
        if (normalized_len <= 0)
        {
            spdlog::error("normalizeForCache: NFC normalization pre-flight failed for input '{}'. Error: {}", input, GetLastError());
            return std::string{input};
        }

        std::wstring normalized_buffer(normalized_len, L'\0');
        normalized_len = NormalizeString(NormalizationC, utf16_buffer.data(), utf16_len, normalized_buffer.data(), normalized_len);
        if (normalized_len <= 0)
        {
            spdlog::error("normalizeForCache: NFC normalization failed for input '{}'. Error: {}", input, GetLastError());
            return std::string{input};
        }
        normalized_buffer.resize(normalized_len);

        // --- Step 3: Case folding using LCMapStringEx with invariant locale ---
        int folded_len = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
                                        normalized_buffer.data(), normalized_len, nullptr, 0, nullptr, nullptr, 0);
        if (folded_len == 0)
        {
            spdlog::error("normalizeForCache: Case folding pre-flight failed for input '{}'. Error: {}", input, GetLastError());
            return std::string{input};
        }

        std::wstring folded_buffer(folded_len, L'\0');
        folded_len = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
                                   normalized_buffer.data(), normalized_len, folded_buffer.data(), folded_len, nullptr, nullptr, 0);
        if (folded_len == 0)
        {
            spdlog::error("normalizeForCache: Case folding failed for input '{}'. Error: {}", input, GetLastError());
            return std::string{input};
        }

        // --- Step 4: Convert back to UTF-8 ---
        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, folded_buffer.data(), folded_len, nullptr, 0, nullptr, nullptr);
        if (utf8_len == 0)
        {
            spdlog::error("normalizeForCache: UTF-16 to UTF-8 conversion failed for input '{}'. Error: {}", input, GetLastError());
            return std::string{input};
        }

        std::string result(utf8_len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, folded_buffer.data(), folded_len, result.data(), utf8_len, nullptr, nullptr);

        return result;
    }

#elif defined(__APPLE__)
    // macOS implementation using CoreFoundation
    std::string normalizeForCache(std::string_view input)
    {
        if (input.empty())
        {
            return std::string{input};
        }

        // Create CFString from UTF-8 input
        CFStringRef cfStr = CFStringCreateWithBytes(kCFAllocatorDefault,
                                                     reinterpret_cast<const UInt8*>(input.data()),
                                                     input.size(),
                                                     kCFStringEncodingUTF8,
                                                     false);
        if (!cfStr)
        {
            spdlog::error("normalizeForCache: Failed to create CFString for input '{}'", input);
            return std::string{input};
        }

        // Create a mutable copy for in-place operations
        CFMutableStringRef mutableStr = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, cfStr);
        CFRelease(cfStr);

        if (!mutableStr)
        {
            spdlog::error("normalizeForCache: Failed to create mutable CFString for input '{}'", input);
            return std::string{input};
        }

        // --- Step 1: NFC Normalization ---
        CFStringNormalize(mutableStr, kCFStringNormalizationFormC);

        // --- Step 2: Case folding ---
        CFStringFold(mutableStr, kCFCompareCaseInsensitive, nullptr);

        // --- Step 3: Convert back to UTF-8 ---
        CFIndex length = CFStringGetLength(mutableStr);
        CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;

        std::string result(maxSize, '\0');
        if (!CFStringGetCString(mutableStr, result.data(), maxSize, kCFStringEncodingUTF8))
        {
            spdlog::error("normalizeForCache: Failed to convert result to UTF-8 for input '{}'", input);
            CFRelease(mutableStr);
            return std::string{input};
        }

        CFRelease(mutableStr);

        // Resize to actual string length (remove null padding)
        result.resize(std::strlen(result.c_str()));

        return result;
    }

#else
    #error "Unsupported platform - normalizeForCache requires Windows or macOS"
#endif

    std::string getLowercaseExtension(const std::filesystem::path &path)
    {
        // path.extension() returns a path object, so we convert it to a string.
        auto extensionString = pathToString(path.extension());

        // If there's no extension, return an empty string immediately.
        if (extensionString.empty())
        {
            return "";
        }

        // Use our robust normalization function. It handles case-folding and
        // all other Unicode normalization forms for us.
        return normalizeForCache(extensionString);
    }

    std::filesystem::path expandPath(const std::string& path)
    {
        if (path.empty())
        {
            return {};
        }

        std::string result = path;

        // Expand ${VAR} syntax (works on all platforms)
        std::regex envVarPattern(R"(\$\{([^}]+)\})");
        std::smatch match;
        std::string::const_iterator searchStart = result.cbegin();

        std::string expanded;
        size_t lastPos = 0;

        while (std::regex_search(searchStart, result.cend(), match, envVarPattern))
        {
            // Append text before the match
            size_t matchPos = static_cast<size_t>(match.position()) + lastPos;
            expanded += result.substr(lastPos, matchPos - lastPos);

            // Get environment variable value
            std::string varName = match[1].str();
            const char* envValue = std::getenv(varName.c_str());
            if (envValue)
            {
                expanded += envValue;
            }
            else
            {
                spdlog::warn("Environment variable ${} not found, leaving as-is", varName);
                expanded += match[0].str();  // Keep original ${VAR} if not found
            }

            lastPos = matchPos + match[0].length();
            searchStart = match.suffix().first;
        }

        // Append remaining text after last match
        expanded += result.substr(lastPos);
        result = expanded;

#if defined(_WIN32) || defined(_WIN64)
        // Normalize forward slashes to backslashes on Windows
        std::replace(result.begin(), result.end(), '/', '\\');
#endif

        return std::filesystem::path{result};
    }

    std::string getDefaultConfigRootTemplate()
    {
#if defined(_WIN32) || defined(_WIN64)
        return "${LOCALAPPDATA}/jucyaudio";
#elif defined(__APPLE__)
        return "${HOME}/Library/Application Support/jucyaudio";
#else
        // Linux and other Unix-like systems
        return "${HOME}/.config/jucyaudio";
#endif
    }

    std::filesystem::path getConfigRoot()
    {
        // Check for JUCYAUDIO_CONFIG environment variable override
        const char* configOverride = std::getenv("JUCYAUDIO_CONFIG");
        if (configOverride && configOverride[0] != '\0')
        {
            spdlog::info("Using JUCYAUDIO_CONFIG override: {}", configOverride);
            return expandPath(configOverride);
        }

        // Use platform-specific default
        return expandPath(getDefaultConfigRootTemplate());
    }
} // namespace jucyaudio
