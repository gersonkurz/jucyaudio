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

        enum class RemoveTrackOption
        {
            RemoveFromBoth = 0,
            RemoveFromMixOnly = 1,
            CancelOperation = 2,
            AskUser = 3
        };

        inline std::string enumAsString(RemoveTrackOption option)
        {
            switch (option)
            {
            case RemoveTrackOption::RemoveFromBoth:
                return "RemoveFromBoth";
            case RemoveTrackOption::RemoveFromMixOnly:
                return "RemoveFromMixOnly";
            case RemoveTrackOption::CancelOperation:
                return "CancelOperation";
            case RemoveTrackOption::AskUser:
                return "AskUser";
            default:
                return "Unknown";
            }
        }

        inline bool enumFromString(std::string_view str, RemoveTrackOption &value)
        {
            if (str == "RemoveFromBoth")
                value = RemoveTrackOption::RemoveFromBoth;
            else if (str == "RemoveFromMixOnly")
                value = RemoveTrackOption::RemoveFromMixOnly;
            else if (str == "CancelOperation")
                value = RemoveTrackOption::CancelOperation;
            else if (str == "AskUser")
                value = RemoveTrackOption::AskUser;
            else
                return false;
            return true;
        }

        template <typename T> struct EnumConfigValue : public IEnumConfigValue
        {
            T value;

            EnumConfigValue(T initialValue) : value(initialValue) {}

            std::string toString() const
            {
                return enumAsString(value);
            }

            bool fromString(std::string_view str)
            {
                return enumFromString(str, value);
            }
        };

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
                logger->info("{}: creating DataViewColumnSection with name '{}' at {}", __func__, name, (const void *)this);
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
                TypedValue<std::string> theme{this, "Theme", "light"};
                TypedValue<bool> showOfflineTracks{this, "ShowOfflineTracks", false};  // Default to hiding offline tracks
                TypedValueVector<DataViewColumnSection> libraryViewColumns{this, "LibraryViewColumns"};
                TypedValueVector<DataViewColumnSection> workingSetsViewColumns{this, "WorkingSetsViewColumns"};
                TypedValueVector<DataViewColumnSection> mixesViewColumns{this, "MixesViewColumns"};
                TypedValueVector<DataViewColumnSection> foldersViewColumns{this, "FoldersViewColumns"};

            } uiSettings{this};

            struct ExportSettings : public Section
            {
                ExportSettings(Section *parent)
                    : Section{parent, "Export"}
                {
                }

                TypedValue<std::string> defaultArtist{this, "DefaultArtist", "Unknown Artist"};
                TypedValue<std::string> defaultAlbum{this, "DefaultAlbum", "Unknown Album"};
                TypedValue<std::string> defaultYear{this, "DefaultYear", "2025"};
                TypedValue<std::string> defaultGenre{this, "DefaultGenre", "Electronic"};
                TypedValue<std::string> defaultComment{this, "DefaultComment", ""};

            } exportSettings{this};

            struct MixEditingSettings : public Section
            {
                MixEditingSettings(Section *parent)
                    : Section{parent, "MixEditing"}
                {
                }

                TypedValue<bool> removeFromWorkingSetOnDelete{this, "RemoveFromWorkingSetOnDelete", true};
                TypedValue<bool> askBeforeRemovingFromWorkingSet{this, "AskBeforeRemovingFromWorkingSet", true};
                TypedValue<bool> clearWorkingSetAfterExport{this, "ClearWorkingSetAfterExport", true};
                TypedValue<EnumConfigValue<RemoveTrackOption>> removeTrackOption{this, "RemoveTrackOption", RemoveTrackOption::RemoveFromBoth};
                TypedValue<bool> useVirtualTimeline{this, "UseVirtualTimeline", false};  // Default to old timeline for now

            } mixEditingSettings{this};

            struct LoggingSettings : public Section
            {
                LoggingSettings(Section *parent)
                    : Section{parent, "Logging"}
                {
                }

                TypedValue<std::string> logLevel{this, "log_level", "info"};

            } loggingSettings{this};

            struct UndoSettings : public Section
            {
                UndoSettings(Section *parent)
                    : Section{parent, "Undo"}
                {
                }

                TypedValue<int> maxOperations{this, "MaxOperations", 100};

            } undoSettings{this};

            struct BackupSettings : public Section
            {
                BackupSettings(Section *parent)
                    : Section{parent, "Backup"}
                {
                }

                TypedValue<int> numberOfBackups{this, "NumberOfBackups", 5};

            } backupSettings{this};
            
            struct AudioSettings : public Section
            {
                AudioSettings(Section *parent)
                    : Section{parent, "Audio"}
                {
                }

                TypedValue<bool> equalizerBypassed{this, "EqualizerBypassed", true};  // Default to bypassed (disabled)
                TypedValue<bool> reverbBypassed{this, "ReverbBypassed", true};        // Default to bypassed (disabled)

            } audioSettings{this};
        };

        extern RootSettings theSettings;

        TypedValueVector<DataViewColumnSection> *getSectionFor(database::INavigationNode *node);
    } // namespace config

} // namespace jucyaudio