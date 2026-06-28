#include <Audio/Plugins/PluginManagerService.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace audio
    {
        PluginManagerService thePluginManagerService;

        PluginManagerService::PluginManagerService() = default;

        void PluginManagerService::initialize(const std::filesystem::path &configRoot)
        {
            if (m_initialized)
            {
                return;
            }

            m_configRoot = configRoot;
            if (!m_configRoot.empty())
            {
                std::error_code ec;
                std::filesystem::create_directories(m_configRoot, ec);
                if (ec)
                {
                    spdlog::error("PluginManagerService: Failed to create config dir {}: {}", m_configRoot.string(), ec.message());
                }
            }

            // JUCE 8.0.14 removed AudioPluginFormatManager::addDefaultFormats() in favour of the
            // free function addDefaultFormatsToManager() (the UI-capable variant; there is also a
            // headless one). We host plugins with editor windows, so use the full version.
            juce::addDefaultFormatsToManager(m_formatManager);
            loadPluginList();

            m_initialized = true;
        }

        bool PluginManagerService::isInitialized() const noexcept
        {
            return m_initialized;
        }

        juce::AudioPluginFormatManager &PluginManagerService::getFormatManager()
        {
            return m_formatManager;
        }

        juce::KnownPluginList &PluginManagerService::getKnownPluginList()
        {
            return m_knownPluginList;
        }

        juce::FileSearchPath PluginManagerService::getDefaultVst3SearchPath()
        {
            auto *format = getVst3Format();
            if (format == nullptr)
            {
                return {};
            }
            return format->getDefaultLocationsToSearch();
        }

        juce::File PluginManagerService::getPluginListFile() const
        {
            if (m_configRoot.empty())
            {
                return {};
            }

            return juce::File{m_configRoot.string()}.getChildFile("plugins.xml");
        }

        juce::File PluginManagerService::getDeadMansPedalFile() const
        {
            if (m_configRoot.empty())
            {
                return {};
            }

            return juce::File{m_configRoot.string()}.getChildFile("plugins.deadmanspedal");
        }

        void PluginManagerService::loadPluginList()
        {
            std::lock_guard<std::mutex> lock{m_listMutex};
            loadPluginListLocked();
        }

        void PluginManagerService::savePluginList()
        {
            std::lock_guard<std::mutex> lock{m_listMutex};
            savePluginListLocked();
        }

        PluginScanResult PluginManagerService::scanVst3Plugins(
            const juce::FileSearchPath &searchPath,
            bool skipKnown,
            std::atomic<bool> &shouldCancel,
            const std::function<void(int progressPercent, const std::string &statusMessage)> &progressCb)
        {
            PluginScanResult result{};

            if (!m_initialized)
            {
                spdlog::warn("PluginManagerService: scan requested before initialize()");
                return result;
            }

            auto *format = getVst3Format();
            if (format == nullptr)
            {
                spdlog::error("PluginManagerService: VST3 format not available");
                return result;
            }

            auto searchPathToUse = searchPath;
            if (searchPathToUse.getNumPaths() == 0)
            {
                searchPathToUse = format->getDefaultLocationsToSearch();
            }

            const auto deadMansPedalFile = getDeadMansPedalFile();

            const int beforeCount = m_knownPluginList.getNumTypes();
            const int beforeBlacklisted = static_cast<int>(m_knownPluginList.getBlacklistedFiles().size());

            juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal(m_knownPluginList, deadMansPedalFile);
            deadMansPedalFile.deleteFile();

            juce::String pluginBeingScanned;
            juce::PluginDirectoryScanner scanner{
                m_knownPluginList,
                *format,
                searchPathToUse,
                skipKnown,
                deadMansPedalFile,
                true};

            while (!shouldCancel.load() && scanner.scanNextFile(skipKnown, pluginBeingScanned))
            {
                ++result.scanned;

                if (progressCb)
                {
                    const int progressPercent = static_cast<int>(scanner.getProgress() * 100.0);
                    const std::string statusMessage = "Scanning: " + pluginBeingScanned.toStdString();
                    progressCb(progressPercent, statusMessage);
                }
            }

            const auto failedFiles = scanner.getFailedFiles();
            result.failed = static_cast<int>(failedFiles.size());
            result.added = m_knownPluginList.getNumTypes() - beforeCount;
            result.blacklistedAdded = static_cast<int>(m_knownPluginList.getBlacklistedFiles().size()) - beforeBlacklisted;

            if (result.failed > 0)
            {
                for (const auto &failedFile : failedFiles)
                {
                    spdlog::warn("PluginManagerService: Failed to scan plugin {}", failedFile.toStdString());
                }
            }

            {
                std::lock_guard<std::mutex> lock{m_listMutex};
                m_knownPluginList.scanFinished();
                savePluginListLocked();
            }

            return result;
        }

        juce::AudioPluginFormat *PluginManagerService::getVst3Format()
        {
            for (auto *format : m_formatManager.getFormats())
            {
                if (format != nullptr && format->getName() == "VST3")
                {
                    return format;
                }
            }
            return nullptr;
        }

        void PluginManagerService::loadPluginListLocked()
        {
            const auto listFile = getPluginListFile();
            if (!listFile.existsAsFile())
            {
                return;
            }

            auto xml = juce::XmlDocument::parse(listFile);
            if (xml == nullptr)
            {
                spdlog::warn("PluginManagerService: Failed to parse {}", listFile.getFullPathName().toStdString());
                return;
            }

            m_knownPluginList.recreateFromXml(*xml);
        }

        void PluginManagerService::savePluginListLocked() const
        {
            const auto listFile = getPluginListFile();
            if (listFile.getParentDirectory().exists() == false)
            {
                listFile.getParentDirectory().createDirectory();
            }

            auto xml = m_knownPluginList.createXml();
            if (xml == nullptr)
            {
                spdlog::warn("PluginManagerService: Failed to serialize plugin list");
                return;
            }

            xml->writeTo(listFile);
        }
    } // namespace audio
} // namespace jucyaudio
