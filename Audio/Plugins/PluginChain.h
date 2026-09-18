#pragma once

#include <Utils/AtomicSharedPtr.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <mutex>
#include <vector>

namespace jucyaudio
{
    namespace audio
    {
        struct PluginChainTestAccess;

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
            std::vector<std::shared_ptr<juce::AudioPluginInstance>> getChainSnapshot() const;
            void setGlobalBypassed(bool bypassed) noexcept;
            bool isGlobalBypassed() const noexcept;
            float getCpuLoad() const noexcept;

            /// @brief Logs the faults processBlock recorded, and forgets them.
            ///
            /// Call from any thread that is allowed to block; never from the audio callback. That is
            /// the whole point of the split - see recordFault below.
            void reportPendingFaults();

            /// @brief How many recorded faults are waiting to be reported.
            ///
            /// Exists so a check can see that processBlock recorded rather than reported. Cheap and
            /// safe to call from anywhere.
            size_t pendingFaultCount() const noexcept;

        private:
            // The self test reads the pending faults' names. reportPendingFaults writes them to the
            // log, and which name comes out is the property worth checking - an address-based lookup
            // at report time would name whatever plugin had been allocated over the dead one.
            friend struct PluginChainTestAccess;

            /// @brief A plugin name, captured when the chain is installed.
            ///
            /// Fixed and copyable, because the audio thread has to take one when a plugin throws.
            /// juce::AudioPluginInstance::getName returns a juce::String by value and allocates, so
            /// the name cannot be read there; and an address cannot stand in for identity, because a
            /// fault can still be pending when its plugin is destroyed and another is allocated over
            /// the top of it - which would report the innocent replacement as the one that threw.
            using PluginName = std::array<char, 128>;

            struct ChainState
            {
                std::vector<std::shared_ptr<juce::AudioPluginInstance>> plugins;
                /// Parallel to plugins, filled in setChain. See PluginName.
                std::vector<PluginName> pluginNames;

                /// @brief The host's own view of which plugins have thrown. Parallel to plugins.
                ///
                /// The audio thread cannot use juce::AudioProcessor::suspendProcessing to stop a
                /// plugin that threw: that writes under callbackLock
                /// (juce_AudioProcessor.cpp:583), and the message thread takes the same lock from
                /// the bypass button (UI/Plugins/PluginChainEditor.cpp:170), from
                /// MasterPluginChainPersistence and from prepareToPlay - so the callback could wait
                /// on it. These flags say the same thing without a lock, and processBlock consults
                /// them where it used to rely on isSuspended alone.
                ///
                /// Host state, not user state: isSuspended stays what the user set, which is also
                /// what MasterPluginChainPersistence saves. A fault is cleared by setChain, which
                /// builds a new ChainState, and by prepareToPlay, which is already where a plugin
                /// gets another chance at a new sample rate or block size.
                ///
                /// Sized in setChain to match plugins. std::atomic<bool> is neither copyable nor
                /// movable, so the vector is constructed at its final size rather than filled
                /// alongside the other two.
                std::vector<std::atomic<bool>> faulted;

                bool prepared{false};
                double sampleRate{0.0};
                int blockSize{0};
            };

            bool configurePlugin(juce::AudioPluginInstance &plugin, double sampleRate, int blockSize) const;

            /// @brief One plugin that threw out of processBlock, in a form the audio thread can write.
            ///
            /// The name is a copy of the one ChainState captured when the chain was installed, not a
            /// pointer to the plugin: juce::AudioPluginInstance::getName returns a juce::String by
            /// value, which allocates, and this is filled in on the audio thread; and an address is
            /// not an identity, since the plugin can be destroyed and another allocated over the top
            /// of it before the fault is reported. The detail is a fixed buffer for the same reason -
            /// what() hands back a const char* and copying it into a std::string would allocate.
            struct PluginFault
            {
                PluginName name{};
                char detail[192]{};
                bool hasDetail{false};
            };

            /// @brief Records a fault without logging it. Safe on the audio thread.
            ///
            /// processBlock used to call spdlog::error here, and processBlock is reached from
            /// PlaybackController::getNextAudioBlock - so a plugin throwing meant a juce::String
            /// allocation, spdlog formatting, a sink mutex and, because the flush threshold equals the
            /// log threshold (Utils/LoggingUtils.cpp:18), an fflush, all on the audio thread at the
            /// moment the user is listening. Once per offending plugin, since the host's fault flag is
            /// set immediately after - but once is a dropout.
            ///
            /// An atomic_flag, tried once, rather than a mutex. try_lock does not block on the way
            /// in - but the matching unlock does not get off that lightly: if another thread has
            /// since blocked on the mutex, the unlocking thread has to wake it, and a contended
            /// unlock goes through the kernel. So holding a mutex on the audio callback at all is
            /// enough, and contention needs only playback and the 60 Hz timer that drains this, not
            /// two plugins throwing at once. test_and_set and clear are plain atomic operations with
            /// no wake path.
            ///
            /// Tried once and never spun on, by either side. The audio thread counts a drop and
            /// carries on; the drain gives up and comes back on the next tick, because nothing here
            /// is urgent.
            void recordFault(const PluginName &name, const char *detail) noexcept;

            static constexpr size_t kMaxRecordedFaults = 8;

            util::AtomicSharedPtr<ChainState> m_state;

            mutable std::mutex m_stateMutex;
            std::atomic<bool> m_globalBypassed{false};
            std::atomic<float> m_cpuLoad{0.0f};

            mutable std::atomic_flag m_faultGuard = ATOMIC_FLAG_INIT;
            std::array<PluginFault, kMaxRecordedFaults> m_faults{};
            size_t m_faultCount{0};
            std::atomic<size_t> m_faultsDropped{0};
        };

        extern PluginChain theMasterPluginChain;
    } // namespace audio
} // namespace jucyaudio
