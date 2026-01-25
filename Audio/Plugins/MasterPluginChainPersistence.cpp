#include <Audio/Plugins/MasterPluginChainPersistence.h>

#include <Audio/Plugins/PluginChain.h>
#include <Audio/Plugins/PluginManagerService.h>
#include <Database/TrackLibrary.h>
#include <Database/Sqlite/SqliteTrackDatabase.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>

namespace jucyaudio
{
    namespace audio
    {
        namespace
        {
            database::SqliteTrackDatabase *getSqliteDatabase()
            {
                auto &trackDb = database::theTrackLibrary.getTrackDatabase();
                auto *sqliteDb = dynamic_cast<database::SqliteTrackDatabase *>(&trackDb);
                if (sqliteDb == nullptr)
                {
                    spdlog::warn("MasterChain: SqliteTrackDatabase not available; persistence disabled");
                }
                return sqliteDb;
            }

            bool configureStereoLayout(juce::AudioPluginInstance &plugin)
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

                return plugin.setBusesLayout(layout);
            }

            std::vector<unsigned char> memoryBlockToVector(const juce::MemoryBlock &block)
            {
                const auto size = block.getSize();
                std::vector<unsigned char> bytes(size);
                if (size > 0)
                {
                    std::memcpy(bytes.data(), block.getData(), size);
                }
                return bytes;
            }
        } // namespace

        bool MasterPluginChainPersistence::saveToDatabase(
            const std::vector<std::shared_ptr<juce::AudioPluginInstance>> &chain)
        {
            auto *sqliteDb = getSqliteDatabase();
            if (sqliteDb == nullptr)
            {
                return false;
            }

            std::vector<database::MasterPluginChainEntry> entries;
            entries.reserve(chain.size());

            for (size_t i = 0; i < chain.size(); ++i)
            {
                const auto &plugin = chain[i];
                if (!plugin)
                {
                    continue;
                }

                const auto desc = plugin->getPluginDescription();

                juce::MemoryBlock state;
                plugin->getStateInformation(state);

                database::MasterPluginChainEntry entry{};
                entry.orderIndex = static_cast<int>(i);
                entry.pluginFormat = desc.pluginFormatName.toStdString();
                entry.identifier = desc.fileOrIdentifier.toStdString();
                entry.name = desc.name.toStdString();
                entry.manufacturer = desc.manufacturerName.toStdString();
                entry.version = desc.version.toStdString();
                entry.isEnabled = !plugin->isSuspended();
                entry.stateBlob = memoryBlockToVector(state);

                entries.emplace_back(std::move(entry));
            }

            const auto ok = sqliteDb->getMasterPluginChainManager().saveChain(entries);
            if (!ok)
            {
                spdlog::error("MasterChain: Failed to save plugin chain");
            }
            return ok;
        }

        bool MasterPluginChainPersistence::saveCurrentChain()
        {
            return saveToDatabase(theMasterPluginChain.getChainSnapshot());
        }

        bool MasterPluginChainPersistence::loadFromDatabase()
        {
            auto *sqliteDb = getSqliteDatabase();
            if (sqliteDb == nullptr)
            {
                return false;
            }

            const auto entries = sqliteDb->getMasterPluginChainManager().loadChain();
            if (entries.empty())
            {
                spdlog::info("MasterChain: No persisted plugins found");
                return true;
            }

            auto &knownList = thePluginManagerService.getKnownPluginList();
            auto &formatManager = thePluginManagerService.getFormatManager();
            const auto prep = theMasterPluginChain.getPreparationState();
            const auto sampleRate = prep.prepared ? prep.sampleRate : 44100.0;
            const auto blockSize = prep.prepared ? prep.blockSize : 512;

            const auto knownTypes = knownList.getTypes();
            std::vector<std::shared_ptr<juce::AudioPluginInstance>> chain;
            chain.reserve(entries.size());

            for (const auto &entry : entries)
            {
                const auto matchIt = std::find_if(
                    knownTypes.begin(),
                    knownTypes.end(),
                    [&entry](const juce::PluginDescription &desc)
                    {
                        return desc.pluginFormatName.toStdString() == entry.pluginFormat &&
                               desc.fileOrIdentifier.toStdString() == entry.identifier;
                    });

                if (matchIt == knownTypes.end())
                {
                    spdlog::warn(
                        "MasterChain: Plugin not found in known list (format='{}', id='{}', name='{}')",
                        entry.pluginFormat,
                        entry.identifier,
                        entry.name);
                    continue;
                }

                juce::String errorMessage;
                auto instance = formatManager.createPluginInstance(*matchIt, sampleRate, blockSize, errorMessage);
                if (!instance)
                {
                    spdlog::warn(
                        "MasterChain: Failed to instantiate '{}' during restore: {}",
                        matchIt->name.toStdString(),
                        errorMessage.toStdString());
                    continue;
                }

                if (!configureStereoLayout(*instance))
                {
                    spdlog::warn(
                        "MasterChain: Restored plugin '{}' does not support stereo layout",
                        matchIt->name.toStdString());
                    continue;
                }

                instance->setPlayConfigDetails(2, 2, sampleRate, blockSize);

                if (!entry.stateBlob.empty())
                {
                    try
                    {
                        instance->setStateInformation(entry.stateBlob.data(), static_cast<int>(entry.stateBlob.size()));
                    }
                    catch (const std::exception &ex)
                    {
                        spdlog::error(
                            "MasterChain: setStateInformation threw for '{}': {}",
                            matchIt->name.toStdString(),
                            ex.what());
                    }
                    catch (...)
                    {
                        spdlog::error(
                            "MasterChain: setStateInformation threw unknown exception for '{}'",
                            matchIt->name.toStdString());
                    }
                }

                if (prep.prepared)
                {
                    instance->prepareToPlay(sampleRate, blockSize);
                }

                instance->suspendProcessing(!entry.isEnabled);
                chain.emplace_back(std::shared_ptr<juce::AudioPluginInstance>{std::move(instance)});
            }

            theMasterPluginChain.setChain(chain);
            spdlog::info("MasterChain: Restored {} plugin(s) from database", chain.size());
            return true;
        }
    } // namespace audio
} // namespace jucyaudio
