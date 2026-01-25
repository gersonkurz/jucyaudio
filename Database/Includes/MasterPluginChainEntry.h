#pragma once

#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        struct MasterPluginChainEntry
        {
            int orderIndex{};
            std::string pluginFormat;
            std::string identifier;
            std::string name;
            std::string manufacturer;
            std::string version;
            bool isEnabled{true};
            std::vector<unsigned char> stateBlob;
        };
    } // namespace database
} // namespace jucyaudio

