#pragma once

// @file jucyaudio/Config/config_backend.h
// @brief defines the interface for configuration backends (Qt, Juce, direct etc.)

#include <cstdint>
#include <string>

namespace jucyaudio
{
    namespace config
    {
        class ConfigBackend
        {
        public:
            ConfigBackend() = default;
            virtual ~ConfigBackend() = default;

            ConfigBackend(const ConfigBackend &) = delete;
            ConfigBackend &operator=(const ConfigBackend &) = delete;
            ConfigBackend(ConfigBackend &&) = delete;
            ConfigBackend &operator=(ConfigBackend &&) = delete;

            virtual bool load(const std::string &path, int32_t &value) = 0;
            virtual bool save(const std::string &path, int32_t value) = 0;

            virtual bool load(const std::string &path, bool &value) = 0;
            virtual bool save(const std::string &path, bool value) = 0;

            virtual bool load(const std::string &path, std::string &value) = 0;
            virtual bool save(const std::string &path, const std::string &value) = 0;

            virtual bool sectionExists(const std::string &path) = 0;
            virtual bool deleteKey(const std::string &path) = 0;
            virtual bool deleteSection(const std::string &path) = 0;
        };

    } // namespace config
} // namespace jucyaudio
