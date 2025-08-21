#pragma once
#include <string>

namespace jucyaudio
{
    /**
     * @brief Sets the global spdlog level from a string.
     * 
     * This function parses a string (e.g., "info", "debug", "warn") and applies
     * the corresponding level to the default spdlog logger.
     * 
     * @param levelStr The desired log level as a string.
     */
    void setLogLevelFromString(const std::string& levelStr);
}
