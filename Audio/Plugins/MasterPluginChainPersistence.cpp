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

            auto chain = buildChain(entries,
                [&](const database::MasterPluginChainEntry &entry) -> std::unique_ptr<juce::AudioPluginInstance>
                {
                    const auto matchIt = std::find_if(knownTypes.begin(),
                        knownTypes.end(),
                        [&entry](const juce::PluginDescription &desc)
                        {
                            return desc.pluginFormatName.toStdString() == entry.pluginFormat && desc.fileOrIdentifier.toStdString() == entry.identifier;
                        });

                    if (matchIt == knownTypes.end())
                    {
                        spdlog::warn(
                            "MasterChain: Plugin not found in known list (format='{}', id='{}', name='{}')", entry.pluginFormat, entry.identifier, entry.name);
                        return nullptr;
                    }

                    juce::String errorMessage;
                    auto instance = formatManager.createPluginInstance(*matchIt, sampleRate, blockSize, errorMessage);
                    if (!instance)
                    {
                        spdlog::warn("MasterChain: Failed to instantiate '{}' during restore: {}", matchIt->name.toStdString(), errorMessage.toStdString());
                    }
                    return instance;
                });

            theMasterPluginChain.setChain(chain);
            spdlog::info("MasterChain: Restored {} plugin(s) from database", chain.size());
            return true;
        }

        std::vector<std::shared_ptr<juce::AudioPluginInstance>> MasterPluginChainPersistence::buildChain(
            const std::vector<database::MasterPluginChainEntry> &entries, const PluginInstanceFactory &createInstance)
        {
            std::vector<std::shared_ptr<juce::AudioPluginInstance>> chain;
            chain.reserve(entries.size());

            for (const auto &entry : entries)
            {
                auto instance = createInstance(entry);
                if (!instance)
                {
                    // Nothing to keep: the plugin is not installed, or the format manager could not
                    // make it. Holding a stored entry across a save so that an absent plugin is not
                    // erased from the chain is a separate question - see issue #64.
                    continue;
                }

                // No layout check here, and no setPlayConfigDetails or prepareToPlay either. This
                // used to refuse a plugin whose buses would not go stereo, before it reached the
                // vector below, so one startup at an awkward sample rate erased it from the user's
                // chain and the next save wrote the shortened list back to the database (#64).
                //
                // PluginChain::setChain configures the plugin when the chain is prepared and records
                // a refusal in ChainState::hostDisabled rather than dropping it (#63), so the plugin
                // keeps its place and is tried again at the next prepare. That leaves the layout in
                // one place instead of two.

                if (!entry.stateBlob.empty())
                {
                    try
                    {
                        instance->setStateInformation(entry.stateBlob.data(), static_cast<int>(entry.stateBlob.size()));
                    }
                    catch (const std::exception &ex)
                    {
                        spdlog::error("MasterChain: setStateInformation threw for '{}': {}", entry.name, ex.what());
                    }
                    catch (...)
                    {
                        spdlog::error("MasterChain: setStateInformation threw unknown exception for '{}'", entry.name);
                    }
                }

                // The user's bypass, restored. This is the one thing that belongs in that field.
                instance->suspendProcessing(!entry.isEnabled);
                chain.emplace_back(std::shared_ptr<juce::AudioPluginInstance>{std::move(instance)});
            }

            return chain;
        }
    } // namespace audio
} // namespace jucyaudio
