#include <Config/toml_backend.h>
#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/Nodes/RootNode.h>
#include <UI/DynamicColumnManager.h>
#include <UI/Settings.h>
#include <unordered_map>
#include <vector>

namespace jucyaudio
{
    namespace ui
    {
        extern std::string g_strConfigFilename;
        namespace
        {
            using namespace database;

            bool getAllColumns(INavigationNode *node, std::vector<DataColumnWithIndex> &columns)
            {
                const auto &allColumns = node->getColumns();
                columns.clear();
                for (ColumnIndex_t i = 0; i < allColumns.size(); ++i)
                {
                    const auto &col = allColumns[i];
                    DataColumnWithIndex colWithIndex;
                    colWithIndex.column = const_cast<DataColumn *>(&col); // Cast away constness, as we need to modify it

                    columns.push_back(colWithIndex);
                }
                return true;
            }

            bool getFromConfig(INavigationNode *node, std::vector<DataColumnWithIndex> &columns,
                               config::TypedValueVector<config::DataViewColumnSection> &configColumns)
            {
                if (configColumns.empty())
                {
                    spdlog::info("DynamicColumnManager: No columns defined in config for node '{}'. Initializing with default columns.", node->getName());
                    config::TomlBackend backend{g_strConfigFilename};
                    if (!config::theSettings.load(backend) || configColumns.empty())
                    {
                        spdlog::info("not loaded or only {} columns defined, initializing with all columns", configColumns.size());
                        configColumns.clear();
                        const auto &allColumns = node->getColumns();
                        for (ColumnIndex_t i = 0; i < allColumns.size(); ++i)
                        {
                            const auto &col = allColumns[i];
                            auto newItem = configColumns.addNew();
                            newItem->columnName.set(col.name);
                            newItem->columnWidth.set(col.defaultWidth);
                        }
                        spdlog::info("Initialized {} columns from node '{}'", configColumns.size(), node->getName());
                        config::theSettings.save(backend);
                    }
                }
                // now translate config columns to DataColumnWithIndex, making use of the full list of columns

                const auto &allColumns = node->getColumns();
                columns.clear();
                for (const auto &cc : configColumns.get())
                {
                    spdlog::debug("DynamicColumnManager: Looking for column '{}' in node '{}'", cc->columnName.get(), node->getName());
                    auto it = std::find_if(allColumns.begin(), allColumns.end(),
                                           [&cc](const DataColumn &col)
                                           {
                                               return col.name == cc->columnName.get();
                                           });
                                           
                    if (it != allColumns.end())
                    {
                        spdlog::debug("DynamicColumnManager: Found column '{}' in node '{}'", cc->columnName.get(), node->getName());
                        DataColumnWithIndex colWithIndex;
                        colWithIndex.column = const_cast<DataColumn *>(&(*it)); // Cast away constness, as we need to modify it
                        columns.push_back(colWithIndex);
                    }
                }
                return !columns.empty();
            }

        } // namespace

        namespace columns
        {
            using namespace database;

            // get actual columns to show for view
            bool get(INavigationNode *node, std::vector<DataColumnWithIndex> &columns)
            {
                const auto nodeName = node->getName();
                const auto pConfigSection{config::getSectionFor(node)};
                if (pConfigSection)
                {
                    spdlog::info("DynamicColumnManager: Using library view columns for node '{}'", nodeName);
                    return getFromConfig(node, columns, *pConfigSection);
                }

                // note: there is no point in storing them in the config, since by definition we couldn't find it the config
                spdlog::warn("DynamicColumnManager: No columns defined for node '{}'. Using default columns (= all of them)", nodeName);
                return getAllColumns(node, columns);
            }

        } // namespace columns
    } // namespace ui
} // namespace jucyaudio
