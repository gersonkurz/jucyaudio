#pragma once

#include <Config/section.h>
#include <Config/typed_value.h>
#include <Config/typed_vector_value.h>
#include <Database/Includes/INavigationNode.h>

namespace jucyaudio
{
    namespace config
    {
        extern std::shared_ptr<spdlog::logger> logger;
        
        struct DataViewColumn
        {
            DataViewColumn() = default;

            std::string name;
            int width{100}; // Default width
        };

        struct DataViewColumnSection : public Section
        {
            DataViewColumnSection(Section *parent, const std::string &name)
                : Section{parent, name}
            {
                logger->info("{}: creating DataViewColumnSection with name '{}' at {}", __func__, name, (const void *) this);
            }
            TypedValue<std::string> columnName{this, "ColumnName", ""};
            TypedValue<int> columnWidth{this, "ColumnWidth", 100};
        };

        class RootSettings : public Section
        {
        public:
            RootSettings()
                : Section{}
            {
            }

            struct DatabaseSettings : public Section
            {
                DatabaseSettings(Section *parent)
                    : Section{parent, "Database"}
                {
                }

                TypedValue<std::string> filename{this, "Filename", ""};

            } database{this};

            struct UiSettings : public Section
            {
                UiSettings(Section *parent)
                    : Section{parent, "UI"}
                {
                }

                TypedValueVector<DataViewColumnSection> libraryViewColumns{this, "LibraryViewColumns"};
                TypedValueVector<DataViewColumnSection> workingSetsViewColumns{this, "WorkingSetsViewColumns"};
                TypedValueVector<DataViewColumnSection> mixesViewColumns{this, "MixesViewColumns"};
                TypedValueVector<DataViewColumnSection> foldersViewColumns{this, "FoldersViewColumns"};

            } uiSettings{this};
        };

        extern RootSettings theSettings;

         TypedValueVector<DataViewColumnSection> *getSectionFor(database::INavigationNode *node);
    } // namespace config

} // namespace jucyaudio
