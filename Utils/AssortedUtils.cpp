#include <Database/Includes/Constants.h>
#include <Utils/AssortedUtils.h>
#include <cassert>
#include <ranges>
#include <spdlog/spdlog.h>
// ICU headers are C-style, so we wrap them for C++ compatibility.
#include <unicode/unorm2.h> // For NFC normalization
#include <unicode/ustring.h> // For case-folding and string conversions

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

    std::string durationToString(const Timestamp_t &tp, const std::string &format)
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

        if (auto text = svtext.data())
        {
            bool is_recording_quoted_string = false;
            auto start = text;
            for (;;)
            {
                char c = *(text++);
                if (!c)
                {
                    if (*start)
                    {
                        result.push_back(start);
                    }
                    break;
                }
                if (is_recording_quoted_string)
                {
                    if (c == '"')
                    {
                        assert(text - start >= 1);
                        result.push_back(std::string{start, (size_t)(text - start - 1)});
                        start = text;
                        is_recording_quoted_string = false;
                    }
                    continue;
                }
                else if (handle_quotation_marks && (c == '"'))
                {
                    assert(text - start >= 1);
                    if (text - start > 1)
                    {
                        result.push_back(std::string{start, (size_t)(text - start - 1)});
                    }
                    start = text;
                    is_recording_quoted_string = true;
                    continue;
                }

                if (std::strchr(svseparators.data(), c))
                {
                    assert(text - start >= 1);
                    result.push_back(std::string{start, (size_t)(text - start - 1)});
                    start = text;
                }
            }
        }
        return result;
    }

    [[nodiscard]] std::optional<std::string> normalizeForCache(std::string_view input)
    {
        if (input.empty())
        {
            return std::string("");
        }

        UErrorCode status = U_ZERO_ERROR;

        // --- Step 1: Normalize to NFC (Unchanged, this part is correct) ---
        const UNormalizer2 *nfcNormalizer = unorm2_getNFCInstance(&status);
        if (U_FAILURE(status))
        { /* ... error handling ... */
            return std::nullopt;
        }

        int32_t utf16_len = 0;
        u_strFromUTF8(nullptr, 0, &utf16_len, input.data(), static_cast<int32_t>(input.size()), &status);
        if (status != U_BUFFER_OVERFLOW_ERROR)
        { /* ... error handling ... */
            return std::nullopt;
        }

        status = U_ZERO_ERROR;
        std::vector<UChar> utf16_buffer(utf16_len + 1);
        u_strFromUTF8(utf16_buffer.data(), utf16_buffer.size(), nullptr, input.data(), static_cast<int32_t>(input.size()), &status);
        if (U_FAILURE(status))
        { /* ... error handling ... */
            return std::nullopt;
        }

        std::vector<UChar> normalized_buffer(utf16_len + 1);
        int32_t normalized_len = unorm2_normalize(nfcNormalizer, utf16_buffer.data(), utf16_len, normalized_buffer.data(), normalized_buffer.size(), &status);
        if (U_FAILURE(status))
        { /* ... error handling ... */
            return std::nullopt;
        }

        // --- Step 2: Case-Fold (Corrected Logic) ---
        int32_t folded_len_needed = 0;
        status = U_ZERO_ERROR;
        folded_len_needed = u_strFoldCase(nullptr, 0, normalized_buffer.data(), normalized_len, U_FOLD_CASE_DEFAULT, &status);

        // ***** THIS IS THE FIX *****
        // Check for the two valid outcomes: overflow (string grew) or success (string is same size or smaller).
        if (status != U_BUFFER_OVERFLOW_ERROR && U_SUCCESS(status))
        {
            // This is the "no-op" case. The string didn't need to expand.
            // The required length is just the original length.
            folded_len_needed = normalized_len;
        }
        else if (status != U_BUFFER_OVERFLOW_ERROR)
        {
            // Any other failure is a real error.
            spdlog::error("normalizeForCache: Case-folding pre-flight failed for input '{}'. ICU Error: {}", input, u_errorName(status));
            return std::nullopt;
        }
        // If we get here, folded_len_needed has the correct required size.

        status = U_ZERO_ERROR;
        std::vector<UChar> folded_buffer(folded_len_needed + 1);
        int32_t folded_len = u_strFoldCase(folded_buffer.data(), folded_buffer.size(), normalized_buffer.data(), normalized_len, U_FOLD_CASE_DEFAULT, &status);
        if (U_FAILURE(status))
        {
            spdlog::error("normalizeForCache: Case-folding failed for input '{}'. ICU Error: {}", input, u_errorName(status));
            return std::nullopt;
        }

        // --- Step 3: Convert back to UTF-8 (Unchanged, this logic is correct) ---
        int32_t utf8_len_needed = 0;
        status = U_ZERO_ERROR;
        u_strToUTF8(nullptr, 0, &utf8_len_needed, folded_buffer.data(), folded_len, &status);
        if (status != U_BUFFER_OVERFLOW_ERROR)
        { /* ... error handling ... */
            return std::nullopt;
        }

        status = U_ZERO_ERROR;
        std::string result(utf8_len_needed, '\0');
        u_strToUTF8(result.data(), result.size(), nullptr, folded_buffer.data(), folded_len, &status);
        if (U_FAILURE(status))
        { /* ... error handling ... */
            return std::nullopt;
        }

        return result;
    }

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
        auto normalizedResult = normalizeForCache(extensionString);

        // If normalization succeeds, return the result.
        // If it fails (which is highly unlikely for a simple extension),
        // return a simple ASCII lowercase version as a fallback.
        if (normalizedResult)
        {
            return *normalizedResult;
        }
        else
        {
            // Fallback for the very rare case that ICU fails.
            spdlog::warn("ICU normalization failed for extension '{}'. Falling back to ASCII lowercase.", extensionString);
            std::transform(extensionString.begin(),
                extensionString.end(),
                extensionString.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });
            return extensionString;
        }
    }
} // namespace jucyaudio
