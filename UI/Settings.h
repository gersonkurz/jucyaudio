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

        enum class VisualizerPlacement
        {
            Bottom = 0,
            Left = 1,
            Right = 2
        };

        inline std::string enumAsString(VisualizerPlacement placement)
        {
            switch (placement)
            {
            case VisualizerPlacement::Bottom:
                return "Bottom";
            case VisualizerPlacement::Left:
                return "Left";
            case VisualizerPlacement::Right:
                return "Right";
            default:
                return "Bottom";
            }
        }

        inline bool enumFromString(std::string_view str, VisualizerPlacement &value)
        {
            if (str == "Bottom")
                value = VisualizerPlacement::Bottom;
            else if (str == "Left")
                value = VisualizerPlacement::Left;
            else if (str == "Right")
                value = VisualizerPlacement::Right;
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

                // Database path using ${VAR} syntax for environment variables
                // Default: ${JUCYAUDIO_CONFIG}/jucyaudio.db
                TypedValue<std::string> filename{this, "Filename", "${JUCYAUDIO_CONFIG}/jucyaudio.db"};

            } database{this};

            struct UiSettings : public Section
            {
                UiSettings(Section *parent)
                    : Section{parent, "UI"}
                {
                }
                TypedValue<std::string> theme{this, "Theme", "gruvbox-dark"};
                TypedValue<bool> showOfflineTracks{this, "ShowOfflineTracks", false};  // Default to hiding offline tracks
                TypedValue<int> visualizerPlacement{this, "VisualizerPlacement", 0};  // 0=Bottom, 1=Left, 2=Right
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
                TypedValue<std::string> lastUsedExportFolder{this, "LastUsedExportFolder", ""};

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
                TypedValue<bool> preloadWaveformsOnMixOpen{this, "PreloadWaveformsOnMixOpen", true};
                TypedValue<bool> drawStereoWaveforms{this, "DrawStereoWaveforms", false}; // Default to combined waveform view
                TypedValue<bool> linkEnvelopePointsToAttachPoints{this, "LinkEnvelopePointsToAttachPoints", true}; // Auto-scale envelope points when attach points move

            } mixEditingSettings{this};

            struct LoggingSettings : public Section
            {
                LoggingSettings(Section *parent)
                    : Section{parent, "Logging"}
                {
                }

                TypedValue<std::string> logLevel{this, "log_level", "info"};

            } loggingSettings{this};

            struct BackupSettings : public Section
            {
                BackupSettings(Section *parent)
                    : Section{parent, "Backup"}
                {
                }

                TypedValue<int> numberOfBackups{this, "NumberOfBackups", 5};
                TypedValue<bool> enablePruning{this, "EnablePruning", true};

            } backupSettings{this};
            
            struct AudioSettings : public Section
            {
                AudioSettings(Section *parent)
                    : Section{parent, "Audio"}
                {
                }

                TypedValue<bool> equalizerBypassed{this, "EqualizerBypassed", true};  // Default to bypassed (disabled)
                TypedValue<bool> reverbBypassed{this, "ReverbBypassed", true};        // Default to bypassed (disabled)
                TypedValue<int> visualizerBufferSize{this, "VisualizerBufferSize", 2048};  // FIFO size for visualizer (samples)
                TypedValue<bool> shuffleMode{this, "ShuffleModeEnabled", false};      // Shuffle mode for playlist playback
                TypedValue<int> repeatMode{this, "RepeatMode", 0};                    // Repeat mode: 0=None, 1=One, 2=All
                TypedValue<int> visualizerPresetAutoSwitchSeconds{this, "VisualizerPresetAutoSwitchSeconds", 5};  // Auto-switch to random preset every N seconds (0 = disabled)
                TypedValue<bool> visualizerSwitchPresetOnTrackChange{this, "VisualizerSwitchPresetOnTrackChange", true};  // Switch to random preset on track change
                TypedValue<std::string> vst3ScanPaths{this, "Vst3ScanPaths", ""};    // Optional override: one path per line

            } audioSettings{this};
            
            struct TileRenderingSettings : public Section
            {
                TileRenderingSettings(Section *parent)
                    : Section{parent, "TileRendering"}
                {
                }

                TypedValue<int> tileCacheSizeMB{this, "TileCacheSizeMB", 512};              // Memory limit for tile cache in MB
                TypedValue<int> tileWidthPixels{this, "TileWidthPixels", 256};              // Width of each rendered tile in pixels
                TypedValue<int> tileDurationSeconds{this, "TileDurationSeconds", 30};       // Duration of audio each tile represents
                TypedValue<int> tileRenderWidth{this, "TileRenderWidth", 512};              // Render resolution for quality
                TypedValue<int> prefetchTileCount{this, "PrefetchTileCount", 2};            // Number of tiles to prefetch on each side
                TypedValue<int> waveformVerticalZoomPercent{this, "WaveformVerticalZoomPercent", 90}; // Vertical zoom as percent (0-100)
                TypedValue<bool> enableTileCache{this, "EnableTileCache", true};            // Enable/disable tile caching
                TypedValue<bool> debugTileRendering{this, "DebugTileRendering", false};     // Show tile boundaries for debugging

            } tileRenderingSettings{this};
        };

        extern RootSettings theSettings;

        TypedValueVector<DataViewColumnSection> *getSectionFor(database::INavigationNode *node);
    } // namespace config

} // namespace jucyaudio
