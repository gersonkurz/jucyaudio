#include <Audio/Plugins/PluginChain.h>
#include <cstring>
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
            newState->pluginNames.reserve(plugins.size());
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

                // The name, taken here rather than when a fault happens: this is the message
                // thread, where getName() may allocate. See PluginName.
                PluginName name{};
                const auto text = plugin->getName().toStdString();
                std::strncpy(name.data(), text.c_str(), name.size() - 1);
                newState->pluginNames.emplace_back(name);
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

        void PluginChain::recordFault(const PluginName &name, const char *detail) noexcept
        {
            if (m_faultGuard.test_and_set(std::memory_order_acquire))
            {
                // Somebody else has it. Not waited for - see the header - and not lost either: the
                // report says how many went unrecorded.
                m_faultsDropped.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            if (m_faultCount >= kMaxRecordedFaults)
            {
                m_faultsDropped.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                auto &fault = m_faults[m_faultCount];
                fault.name = name;
                fault.hasDetail = detail != nullptr;
                fault.detail[0] = '\0';
                if (detail != nullptr)
                {
                    // Truncating rather than allocating. what() is whatever the plugin's exception
                    // says and there is no bound on it.
                    std::strncpy(fault.detail, detail, sizeof(fault.detail) - 1);
                    fault.detail[sizeof(fault.detail) - 1] = '\0';
                }
                ++m_faultCount;
            }

            m_faultGuard.clear(std::memory_order_release);
        }

        size_t PluginChain::pendingFaultCount() const noexcept
        {
            if (m_faultGuard.test_and_set(std::memory_order_acquire))
            {
                return 0;
            }
            const auto count = m_faultCount;
            m_faultGuard.clear(std::memory_order_release);
            return count;
        }

        void PluginChain::reportPendingFaults()
        {
            std::array<PluginFault, kMaxRecordedFaults> taken{};
            size_t count = 0;
            size_t dropped = 0;

            if (m_faultGuard.test_and_set(std::memory_order_acquire))
            {
                // The audio thread is mid-record. Left for the next tick rather than waited for -
                // waiting is the thing this whole arrangement exists to avoid, on either side.
                return;
            }

            if (m_faultCount == 0 && m_faultsDropped.load(std::memory_order_relaxed) == 0)
            {
                m_faultGuard.clear(std::memory_order_release);
                return;
            }

            taken = m_faults;
            count = m_faultCount;
            m_faultCount = 0;
            dropped = m_faultsDropped.exchange(0, std::memory_order_relaxed);
            m_faultGuard.clear(std::memory_order_release);

            for (size_t i = 0; i < count; ++i)
            {
                const auto &fault = taken[i];

                // The name travelled with the fault, captured when the chain was installed. Looking
                // it up here by address would misattribute: a fault can still be pending when its
                // plugin is destroyed and another is allocated at the same address, and the lookup
                // would name the replacement as the one that threw.
                const std::string name{fault.name.data()};

                if (fault.hasDetail)
                {
                    spdlog::error("PluginChain: Plugin '{}' threw in processBlock and was suspended: {}", name, fault.detail);
                }
                else
                {
                    spdlog::error("PluginChain: Plugin '{}' threw an unknown exception in processBlock and was suspended", name);
                }
            }

            if (dropped > 0)
            {
                spdlog::error("PluginChain: {} further plugin fault(s) were not recorded", dropped);
            }
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
            for (size_t index = 0; index < state->plugins.size(); ++index)
            {
                const auto &plugin = state->plugins[index];
                if (plugin && !plugin->isSuspended())
                {
                    try
                    {
                        plugin->processBlock(buffer, midiBuffer);
                    }
                    catch (const std::exception &ex)
                    {
                        // Recorded, not logged: this runs on the audio thread. See recordFault. The
                        // name comes from the chain, where it was captured off this thread.
                        recordFault(state->pluginNames[index], ex.what());
                        plugin->suspendProcessing(true);
                    }
                    catch (...)
                    {
                        recordFault(state->pluginNames[index], nullptr);
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
