#include <Audio/Plugins/PluginChain.h>
#include <spdlog/spdlog.h>

#include <chrono>

namespace jucyaudio
{
    namespace audio
    {
        PluginChain theMasterPluginChain;

        PluginChain::PluginChain()
            : m_state{std::make_shared<ChainState>()}
        {
        }

        void PluginChain::setChain(const std::vector<std::shared_ptr<juce::AudioPluginInstance>> &plugins)
        {
            std::shared_ptr<ChainState> current;
            bool wasPrepared = false;
            double previousSampleRate = 0.0;
            int previousBlockSize = 0;
            {
                std::lock_guard<std::mutex> lock{m_stateMutex};
                current = m_state.load();
                if (current && current->prepared)
                {
                    wasPrepared = true;
                    previousSampleRate = current->sampleRate;
                    previousBlockSize = current->blockSize;
                    for (const auto &plugin : current->plugins)
                    {
                        if (plugin)
                        {
                            plugin->releaseResources();
                        }
                    }
                    current->prepared = false;
                }
            }

            auto newState = std::make_shared<ChainState>();
            {
                if (current)
                {
                    newState->prepared = wasPrepared;
                    newState->sampleRate = wasPrepared ? previousSampleRate : current->sampleRate;
                    newState->blockSize = wasPrepared ? previousBlockSize : current->blockSize;
                }
            }

            newState->plugins.reserve(plugins.size());
            for (const auto &plugin : plugins)
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

                newState->plugins.emplace_back(plugin);
            }

            m_state.store(std::move(newState));
        }

        void PluginChain::clear()
        {
            std::shared_ptr<ChainState> current;
            {
                std::lock_guard<std::mutex> lock{m_stateMutex};
                current = m_state.load();
                if (current && current->prepared)
                {
                    for (const auto &plugin : current->plugins)
                    {
                        if (plugin)
                        {
                            plugin->releaseResources();
                        }
                    }
                    current->prepared = false;
                }
            }

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
            if (m_globalBypassed.load(std::memory_order_acquire))
            {
                return;
            }

            auto state = m_state.load();
            if (!state || state->plugins.empty())
            {
                return;
            }

            if (buffer.getNumChannels() < 2)
            {
                return;
            }

            const auto startTime = std::chrono::steady_clock::now();
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

            const auto endTime = std::chrono::steady_clock::now();
            const auto sampleRate = state->sampleRate;
            const auto blockSize = state->blockSize;
            if (sampleRate > 0.0 && blockSize > 0)
            {
                const std::chrono::duration<double> elapsed = endTime - startTime;
                const auto blockDurationSeconds = static_cast<double>(blockSize) / sampleRate;
                if (blockDurationSeconds > 0.0)
                {
                    const auto load = static_cast<float>(elapsed.count() / blockDurationSeconds);
                    const auto previous = m_cpuLoad.load(std::memory_order_relaxed);
                    const auto smoothed = previous * 0.9f + load * 0.1f;
                    m_cpuLoad.store(smoothed, std::memory_order_relaxed);
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

        std::vector<std::shared_ptr<juce::AudioPluginInstance>> PluginChain::getChainSnapshot() const
        {
            const auto state = m_state.load();
            if (!state)
            {
                return {};
            }
            return state->plugins;
        }

        void PluginChain::setGlobalBypassed(bool bypassed) noexcept
        {
            m_globalBypassed.store(bypassed, std::memory_order_release);
        }

        bool PluginChain::isGlobalBypassed() const noexcept
        {
            return m_globalBypassed.load(std::memory_order_acquire);
        }

        float PluginChain::getCpuLoad() const noexcept
        {
            return m_cpuLoad.load(std::memory_order_relaxed);
        }

        bool PluginChain::configurePlugin(juce::AudioPluginInstance &plugin, double sampleRate, int blockSize) const
        {
            juce::AudioProcessor::BusesLayout layout;
            const auto inputBusCount = plugin.getBusCount(true);
            const auto outputBusCount = plugin.getBusCount(false);
            layout.inputBuses.clearQuick();
            layout.outputBuses.clearQuick();
            for (int i = 0; i < inputBusCount; ++i)
            {
                layout.inputBuses.add(juce::AudioChannelSet::disabled());
            }
            for (int i = 0; i < outputBusCount; ++i)
            {
                layout.outputBuses.add(juce::AudioChannelSet::disabled());
            }

            if (inputBusCount > 0)
            {
                layout.inputBuses.set(0, juce::AudioChannelSet::stereo());
            }
            if (outputBusCount > 0)
            {
                layout.outputBuses.set(0, juce::AudioChannelSet::stereo());
            }

            if (!plugin.setBusesLayout(layout))
            {
                return false;
            }

            plugin.setPlayConfigDetails(2, 2, sampleRate, blockSize);
            return true;
        }
    } // namespace audio
} // namespace jucyaudio
