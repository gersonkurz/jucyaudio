#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <Database/Includes/MasterPluginChainEntry.h>

#include <functional>
#include <memory>
#include <vector>

namespace jucyaudio
{
    namespace audio
    {
        class MasterPluginChainPersistence final
        {
        public:
            /// @brief Makes the plugin a stored entry names, or nullptr if it cannot be made.
            using PluginInstanceFactory = std::function<std::unique_ptr<juce::AudioPluginInstance>(const database::MasterPluginChainEntry &)>;

            static bool loadFromDatabase();
            static bool saveToDatabase(const std::vector<std::shared_ptr<juce::AudioPluginInstance>> &chain);
            static bool saveCurrentChain();

            /// @brief Turns stored entries into the chain PluginChain::setChain is given.
            ///
            /// Split out from loadFromDatabase, which supplies a factory that goes through the known
            /// plugin list and juce::AudioPluginFormatManager. Everything the restore decides happens
            /// here, so it can be driven against stub plugins; the factory is the only part that
            /// cannot.
            static std::vector<std::shared_ptr<juce::AudioPluginInstance>> buildChain(
                const std::vector<database::MasterPluginChainEntry> &entries, const PluginInstanceFactory &createInstance);
        };
    } // namespace audio
} // namespace jucyaudio

