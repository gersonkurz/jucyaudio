#pragma once

#include <Utils/AtomicSharedPtr.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <mutex>
#include <vector>

namespace jucyaudio
{
    namespace audio
    {
        class PluginChain final
        {
        public:
            struct PreparationState
            {
                bool prepared{false};
                double sampleRate{0.0};
                int blockSize{0};
            };

            PluginChain();

            void setChain(const std::vector<std::shared_ptr<juce::AudioPluginInstance>> &plugins);
            void clear();

            void prepareToPlay(double sampleRate, int blockSize);
            void releaseResources();
            void processBlock(juce::AudioBuffer<float> &buffer);

            bool isEmpty() const;
            PreparationState getPreparationState() const;

        private:
            struct ChainState
            {
                std::vector<std::shared_ptr<juce::AudioPluginInstance>> plugins;
                bool prepared{false};
                double sampleRate{0.0};
                int blockSize{0};
            };

            bool configurePlugin(juce::AudioPluginInstance &plugin, double sampleRate, int blockSize) const;

            util::AtomicSharedPtr<ChainState> m_state;
            mutable std::mutex m_stateMutex;
        };

        extern PluginChain theMasterPluginChain;
    } // namespace audio
} // namespace jucyaudio
