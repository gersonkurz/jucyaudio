#include <Database/Includes/Constants.h>
#include <Utils/AssortedUtils.h>
#include <cassert>
#include <iomanip> // For std::put_time
#include <ranges>
#include <sstream> // For std::ostringstream

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

    std::vector<std::string> split_string(std::string_view svtext, std::string_view svseparators, bool handle_quotation_marks)
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
} // namespace jucyaudio
