#include "LoggingUtils.h"
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    void setLogLevelFromString(const std::string& levelStr)
    {
        auto level = spdlog::level::from_str(levelStr);

        // spdlog::level::from_str returns 'off' for unrecognized strings.
        // We'll default to 'info' in that case, unless the string was actually "off".
        if (level == spdlog::level::off && levelStr != "off")
        {
            level = spdlog::level::info;
            spdlog::warn("Unrecognized log level '{}', defaulting to 'info'.", levelStr);
        }

        spdlog::set_level(level);
        spdlog::default_logger()->flush_on(level);

        spdlog::info("Log level dynamically set to \"{}\"", spdlog::level::to_string_view(level));
    }
}

