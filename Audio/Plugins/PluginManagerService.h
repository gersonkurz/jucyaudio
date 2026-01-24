#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

namespace jucyaudio
{
    namespace audio
    {
        struct PluginScanResult
        {
            int scanned = 0;
            int added = 0;
            int failed = 0;
            int blacklistedAdded = 0;
        };

        class PluginManagerService final
        {
        public:
            PluginManagerService();

            void initialize(const std::filesystem::path &configRoot);
            bool isInitialized() const noexcept;

            juce::AudioPluginFormatManager &getFormatManager();
            juce::KnownPluginList &getKnownPluginList();

            juce::FileSearchPath getDefaultVst3SearchPath();
            juce::File getPluginListFile() const;
            juce::File getDeadMansPedalFile() const;

            void loadPluginList();
            void savePluginList();

            PluginScanResult scanVst3Plugins(
                const juce::FileSearchPath &searchPath,
                bool skipKnown,
                std::atomic<bool> &shouldCancel,
                const std::function<void(int progressPercent, const std::string &statusMessage)> &progressCb);

        private:
            juce::AudioPluginFormat *getVst3Format();
            void loadPluginListLocked();
            void savePluginListLocked() const;

            juce::AudioPluginFormatManager m_formatManager;
            juce::KnownPluginList m_knownPluginList;
            std::filesystem::path m_configRoot;
            mutable std::mutex m_listMutex;
            bool m_initialized = false;
        };

        extern PluginManagerService thePluginManagerService;
    } // namespace audio
} // namespace jucyaudio
