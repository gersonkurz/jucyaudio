#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

namespace jucyaudio
{
    namespace audio
    {
        class MasterPluginChainPersistence final
        {
        public:
            static bool loadFromDatabase();
            static bool saveToDatabase(const std::vector<std::shared_ptr<juce::AudioPluginInstance>> &chain);
            static bool saveCurrentChain();
        };
    } // namespace audio
} // namespace jucyaudio

