#include <Audio/Plugins/PluginChain.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace audio
    {
        PluginChain theMasterPluginChain;

        PluginChain::PluginChain()
            : m_state{std::make_shared<ChainState>()}
        {
        }

        void PluginChain::setChain(std::vector<std::unique_ptr<juce::AudioPluginInstance>> plugins)
        {
            auto newState = std::make_shared<ChainState>();
            {
                const auto current = m_state.load();
                if (current)
                {
                    newState->prepared = current->prepared;
                    newState->sampleRate = current->sampleRate;
                    newState->blockSize = current->blockSize;
                }
            }

            newState->plugins.reserve(plugins.size());
            for (auto &plugin : plugins)
            {
                if (!plugin)
                {
                    continue;
                }

                if (newState->prepared)
                {
                    if (!configurePlugin(*plugin, newState->sampleRate, newState->blockSize))
                    {
                        spdlog::warn("PluginChain: Skipping plugin '{}' (unsupported layout)", plugin->getName().toStdString());
                        continue;
                    }
                    plugin->suspendProcessing(false);
                    plugin->prepareToPlay(newState->sampleRate, newState->blockSize);
                }

                newState->plugins.emplace_back(std::move(plugin));
            }

            m_state.store(std::move(newState));
        }

        void PluginChain::clear()
        {
            auto newState = std::make_shared<ChainState>();
            m_state.store(std::move(newState));
        }

        void PluginChain::prepareToPlay(double sampleRate, int blockSize)
        {
            std::lock_guard<std::mutex> lock{m_stateMutex};
            auto state = m_state.load();
            if (!state)
            {
                return;
            }
            state->sampleRate = sampleRate;
            state->blockSize = blockSize;
            state->prepared = true;

            for (const auto &plugin : state->plugins)
            {
                if (!plugin)
                {
                    continue;
                }

                if (!configurePlugin(*plugin, sampleRate, blockSize))
                {
                    spdlog::warn("PluginChain: Plugin '{}' does not support stereo layout", plugin->getName().toStdString());
                    plugin->suspendProcessing(true);
                    continue;
                }

                plugin->suspendProcessing(false);
                plugin->prepareToPlay(sampleRate, blockSize);
            }
        }

        void PluginChain::releaseResources()
        {
            auto state = m_state.load();
            if (!state)
            {
                return;
            }

            std::lock_guard<std::mutex> lock{m_stateMutex};
            for (const auto &plugin : state->plugins)
            {
                if (plugin)
                {
                    plugin->releaseResources();
                }
            }
            state->prepared = false;
        }

        void PluginChain::processBlock(juce::AudioBuffer<float> &buffer)
        {
            auto state = m_state.load();
            if (!state || state->plugins.empty())
            {
                return;
            }

            if (buffer.getNumChannels() < 2)
            {
                return;
            }

            juce::MidiBuffer midiBuffer;
            for (const auto &plugin : state->plugins)
            {
                if (plugin && !plugin->isSuspended())
                {
                    try
                    {
                        plugin->processBlock(buffer, midiBuffer);
                    }
                    catch (const std::exception &ex)
                    {
                        spdlog::error("PluginChain: Plugin '{}' threw in processBlock: {}", plugin->getName().toStdString(), ex.what());
                        plugin->suspendProcessing(true);
                    }
                    catch (...)
                    {
                        spdlog::error("PluginChain: Plugin '{}' threw unknown exception in processBlock", plugin->getName().toStdString());
                        plugin->suspendProcessing(true);
                    }
                }
            }
        }

        bool PluginChain::isEmpty() const
        {
            auto state = m_state.load();
            return !state || state->plugins.empty();
        }

        PluginChain::PreparationState PluginChain::getPreparationState() const
        {
            PreparationState result{};
            const auto state = m_state.load();
            if (state)
            {
                result.prepared = state->prepared;
                result.sampleRate = state->sampleRate;
                result.blockSize = state->blockSize;
            }
            return result;
        }

        bool PluginChain::configurePlugin(juce::AudioPluginInstance &plugin, double sampleRate, int blockSize) const
        {
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add(juce::AudioChannelSet::stereo());
            layout.outputBuses.add(juce::AudioChannelSet::stereo());

            if (!plugin.setBusesLayout(layout))
            {
                return false;
            }

            plugin.setPlayConfigDetails(2, 2, sampleRate, blockSize);
            return true;
        }
    } // namespace audio
} // namespace jucyaudio
