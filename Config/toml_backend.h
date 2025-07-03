#pragma once
#include <Config/config_backend.h>
#include <toml++/toml.hpp>
#include <spdlog/spdlog.h>
#include <source_location>

namespace jucyaudio
{
    namespace config
    {
        extern std::shared_ptr<spdlog::logger> logger;

        class TomlBackend : public ConfigBackend
        {
        public:
            explicit TomlBackend(const std::string &filename)
                : m_filename{filename},
                  m_config{}
            {
                loadFromFile();
            }

            bool load(const std::string &path, int32_t &value) override
            {
                auto loc = std::source_location::current();
                logger->info("{}: Loading int32_t from path: {}", loc.function_name(), path);
                if (auto val = getValueAtPath(path))
                {
                    logger->info("{}: getValueAtPath {} returned {}", loc.function_name(), path, (const void*) val);
                    if (val->is_integer())
                    {
                        value = static_cast<int32_t>(val->as_integer()->get());
                        logger->info("{}: Loaded '{}' from path: {}", loc.function_name(), value, path);
                        return true;
                    }
                }
                logger->warn("Failed to load value from path: {}", path);
                return false;
            }

            bool save(const std::string &path, int32_t value) override
            {
                logger->info("Set {} at path: {}", value, path);
                return setValueAtPath(path, value);
            }

            bool load(const std::string &path, bool &value) override
            {
                auto loc = std::source_location::current();
                logger->info("{}: Loading bool from path: {}", loc.function_name(), path);
                if (auto val = getValueAtPath(path))
                {
                    logger->info("{}: getValueAtPath {} returned {}", loc.function_name(), path, (const void*) val);
                    if (val->is_boolean())
                    {
                        value = val->as_boolean()->get();
                        logger->info("{}: Loaded '{}' from path: {}", loc.function_name(), value, path);
                        return true;
                    }
                }
                logger->warn("Failed to load value from path: {}", path);
                return false;
            }

            bool save(const std::string &path, bool value) override
            {
                logger->info("Set {} at path: {}", value, path);
                return setValueAtPath(path, value);
            }

            bool load(const std::string &path, std::string &value) override
            {                
                auto loc = std::source_location::current();
                logger->info("{}: Loading std::string from path: {}", loc.function_name(), path);
                if (auto val = getValueAtPath(path))
                {
                    logger->info("{}: getValueAtPath {} returned {}", loc.function_name(), path, (const void*) val);
                    if (val->is_string())
                    {
                        value = val->as_string()->get();
                        logger->info("{}: Loaded '{}' from path: {}", loc.function_name(), value, path);
                        return true;
                    }
                }
                logger->warn("Failed to load value from path: {}", path);
                return false;
            }

            bool save(const std::string &path, const std::string &value) override
            {
                logger->info("Set {} at path: {}", value, path);
                return setValueAtPath(path, value);
            }

            bool sectionExists(const std::string &path) override
            {
                auto loc = std::source_location::current();
                logger->info("{}: called for {}", loc.function_name(), path);
                auto parts = splitPath(path);
                if (parts.empty())
                {
                    logger->warn("{}: Path is empty, cannot check section existence.", loc.function_name());
                    return false;
                }

                toml::node *current = &m_config;

                // Navigate through all path parts
                for (const auto &part : parts)
                {
                    logger->info("{}: Looking at part '{}'", loc.function_name(), part);
                    if (auto table = current->as_table())
                    {
                        auto it = table->find(part);
                        if (it != table->end())
                        {
                            current = &it->second;
                        }
                        else
                        {
                            logger->info("{}: Section '{}' does not exist in the config.", loc.function_name(), part);
                            return false; // Path doesn't exist
                        }
                    }
                    else
                    {
                        logger->warn("{}: Section '{}' is not a table", loc.function_name(), part);
                        return false; // Not a table
                    }
                }

                // Check if final node is a table (section)
                const auto success = current->is_table();
                if(success)
                {
                    logger->info("{}: Section '{}' exists in the config.", loc.function_name(), path);
                }
                else
                {
                    logger->warn("{}: Section '{}' is not a table", loc.function_name(), path);
                }
                return success;
            }

            bool deleteKey(const std::string &path) override
            {
                auto loc = std::source_location::current();
                logger->info("{}: called for {}", loc.function_name(), path);
                auto parts = splitPath(path);
                if (parts.empty())
                    return false;

                toml::table *current = &m_config;

                // Navigate to parent table
                for (size_t i = 0; i < parts.size() - 1; ++i)
                {
                    auto it = current->find(parts[i]);
                    if (it != current->end())
                    {
                        if (auto table = it->second.as_table())
                        {
                            current = table;
                        }
                        else
                        {
                            return false; // Path doesn't exist or not a table
                        }
                    }
                    else
                    {
                        return false; // Path doesn't exist
                    }
                }

                // Remove the final key
                bool success = current->erase(parts.back());
                if (success)
                {
                    saveToFile();
                }
                return success;
            }

            bool deleteSection(const std::string &path) override
            {
                auto loc = std::source_location::current();
                logger->info("{}: called for {}", loc.function_name(), path);
                auto parts = splitPath(path);
                if (parts.empty())
                    return false;

                toml::table *current = &m_config;

                // Navigate to parent table
                for (size_t i = 0; i < parts.size() - 1; ++i)
                {
                    auto it = current->find(parts[i]);
                    if (it != current->end())
                    {
                        if (auto table = it->second.as_table())
                        {
                            current = table;
                        }
                        else
                        {
                            return false; // Path doesn't exist or not a table
                        }
                    }
                    else
                    {
                        return false; // Path doesn't exist
                    }
                }

                // Remove the entire section
                bool success = current->erase(parts.back());
                if (success)
                {
                    saveToFile();
                }
                return success;
            }

        private:
            const std::string m_filename;
            toml::table m_config;

            void loadFromFile()
            {
                try
                {
                    if (std::filesystem::exists(m_filename))
                    {
                        m_config = toml::parse_file(m_filename);
                    }
                }
                catch (const toml::parse_error &e)
                {
                    // Log error but continue with empty config
                    m_config = toml::table{};
                }
            }

            void saveToFile()
            {
                std::ofstream file(m_filename);
                if (file.is_open())
                {
                    file << m_config;
                }
            }

            static std::vector<std::string> splitPath(const std::string &path)
            {
                std::vector<std::string> parts;
                std::stringstream ss{path};
                std::string part;

                while (std::getline(ss, part, '/'))
                {
                    if (!part.empty())
                    {
                        parts.push_back(part);
                    }
                }
                return parts;
            }

            toml::node *getValueAtPath(const std::string &path)
            {
                auto loc = std::source_location::current();
                logger->info("{}: called for {}", loc.function_name(), path);

                auto parts = splitPath(path);
                if (parts.empty())
                {
                    logger->error("{}: Path is empty, cannot retrieve value.", loc.function_name());
                    return nullptr;
                }

                toml::node *current = &m_config;

                // Navigate to the parent table
                for (size_t i = 0; i < parts.size() - 1; ++i)
                {
                    logger->info("{} Navigating to part: {}", loc.function_name(), parts[i]);
                    if (const auto& table = current->as_table())
                    {
                        auto it = table->find(parts[i]);
                        if (it != table->end())
                        {
                            current = &it->second;
                        }
                        else
                        {
                            logger->warn("{}: Path '{}' does not exist in the config.", loc.function_name(), parts[i]);
                            return nullptr; // Path doesn't exist
                        }
                    }
                    else
                    {
                        logger->warn("{}: Path '{}' is not a table", loc.function_name(), parts[i]);
                        return nullptr; // Not a table
                    }
                }

                // Get the final value
                if (auto table = current->as_table())
                {
                    auto it = table->find(parts.back());
                    if (it != table->end())
                    {
                        logger->info("{}: Found value at path: {}", loc.function_name(), path);
                        return &it->second;
                    }
                }
                logger->warn("{}: Key '{}' does not exist in the config.", loc.function_name(), parts.back());
                return nullptr;
            }

            template <typename T> bool setValueAtPath(const std::string &path, const T &value)
            {
                auto parts = splitPath(path);
                if (parts.empty())
                    return false;

                toml::table *current = &m_config;

                // Navigate/create the nested structure
                for (size_t i = 0; i < parts.size() - 1; ++i)
                {
                    auto it = current->find(parts[i]);
                    if (it != current->end())
                    {
                        // Path exists, check if it's a table
                        if (auto table = it->second.as_table())
                        {
                            current = table;
                        }
                        else
                        {
                            // Exists but not a table, can't continue
                            return false;
                        }
                    }
                    else
                    {
                        // Create new table
                        auto [inserted_it, success] = current->insert(parts[i], toml::table{});
                        if (success)
                        {
                            current = inserted_it->second.as_table();
                        }
                        else
                        {
                            return false;
                        }
                    }
                }

                // Set the final value
                current->insert_or_assign(parts.back(), value);
                saveToFile();
                return true;
            }
        };
    } // namespace config
} // namespace jucyaudio
