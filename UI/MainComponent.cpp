#include <Config/toml_backend.h>
#include <Database/BackgroundService.h>
#include <Database/BackgroundTasks/BpmAnalysis.h>
#include <Database/BackgroundTasks/BpmAnalysisTask.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Nodes/AlbumsNode.h>
#include <Database/Nodes/MixNode.h>
#include <Database/Nodes/RootNode.h>
#include <Database/Nodes/TypedOverviewNode.h>
#include <Database/Nodes/VirtualFolderNode.h>
#include <Database/Nodes/VirtualFoldersOverview.h>
#include <Database/Nodes/WorkingSetNode.h>
#include <Database/Sqlite/SqliteEQPresetManager.h>
#include <Database/Sqlite/SqliteReverbPresetManager.h>
#include <Database/Sqlite/SqliteTrackDatabase.h>
#include <Database/UndoManager.h>
#include <UI/AboutDialog.h>
#include <UI/CheckboxLookAndFeel.h>
#include <UI/ThemeBrowserDialog.h>
#include <UI/DatabaseMaintenanceDialog.h>
#include <UI/TrackDetailsDialog.h>
#include <Database/DatabaseBackupManager.h>
#include <UI/ColumnConfiguratorDialog.h>
#include <Database/BackgroundTasks/MissingFileCheckTask.h>
#include <UI/CreateMixDialogComponent.h>
#include <UI/CreateWorkingSetDialogComponent.h>
#include <UI/EditMixMetaDataDialog.h>
#include <UI/EditWorkingSetMetaDataDialog.h>
#include <UI/EqualizerDialog.h>
#include <UI/BatchExportDialog.h>
#include <UI/BatchExportProgressDialog.h>
#include <UI/ExportMixDialog.h>
#include <UI/ILongRunningTask.h>
#include <UI/LibraryRootsComponent.h>
#include <UI/MainComponent.h>
#include <UI/MarkerEditDialog.h>
#include <UI/Plugins/PluginScanDialog.h>
#include <UI/Plugins/PluginChainEditor.h>
#include <UI/ReverbDialog.h>
#include <UI/Settings.h>
#include <UI/SettingsDialog.h>
#include <UI/SingletonDialog.h>
#include <UI/TaskDialog.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>
#include <algorithm>
#include <format>
#include <random>
#include <spdlog/spdlog.h>
#ifndef JUCE_WINDOWS
#include <unistd.h>
#endif

namespace jucyaudio
{
    using namespace database;

    namespace // anonymous
    {
        // @brief This is the actual last-known view type, used internally
        ui::MainViewType lastKnownViewType{ui::MainViewType::DataView};

        // @brief This is the last-known view type FOR MIXES ONLY
        ui::MainViewType lastKnownViewTypeForMixes{ui::MainViewType::MixEditor};

        /// Format duration as DDD:HH:MM:SS
        std::string formatDuration(int64_t totalMs)
        {
            const int64_t totalSecs = totalMs / 1000;
            const int days = static_cast<int>(totalSecs / 86400);
            const int hours = static_cast<int>((totalSecs % 86400) / 3600);
            const int mins = static_cast<int>((totalSecs % 3600) / 60);
            const int secs = static_cast<int>(totalSecs % 60);

            return std::format("{}:{:02}:{:02}:{:02}", days, hours, mins, secs);
        }

        /// Format file size in GB with one decimal place
        std::string formatSize(uint64_t bytes)
        {
            const double gb = bytes / 1073741824.0;
            return std::format("{:.1f} GB", gb);
        }

        /// Format aggregate stats as "N Tracks, X GB, D:HH:MM:SS"
        std::string formatAggregateStats(const database::AggregateStats& stats)
        {
            return std::format("{:L} Tracks, {}, {}",
                stats.totalTracks,
                formatSize(stats.totalBytes),
                formatDuration(stats.totalDurationMs));
        }

        /// Format album aggregate stats as "N Albums, T Tracks, D:HH:MM:SS"
        std::string formatAlbumAggregateStats(const database::AlbumAggregateStats& stats)
        {
            return std::format("{:L} Albums, {:L} Tracks, {}",
                stats.totalAlbums,
                stats.totalTracks,
                formatDuration(stats.totalDurationMs));
        }

        /// @brief What the status bar says about the mix being edited.
        ///
        /// The mix, and nothing else. This used to add the source working set's size to the mix's
        /// track count and present the sum as a total to work through - "112/20455 tracks mixed,
        /// 20343 remaining". A working set is a library of candidates, not a queue: those 20343
        /// tracks were never going into this mix, so the total and the remainder were both numbers
        /// that had never meant anything.
        std::string formatMixProgressStatus(const database::MixInfo& mixInfo)
        {
            return std::format("{:L} tracks in mix, {} total mix length",
                mixInfo.numberOfTracks,
                durationToString(mixInfo.totalDuration));
        }

        juce::String formatTrackDetails(const database::TrackInfo& trackInfo)
        {
            const auto& trackDb = theTrackLibrary.getTrackDatabase();
            const auto fullPath = trackInfo.reconstructFullPath(trackDb);
            const juce::String bitrateText = trackInfo.bitrate > 0 ? juce::String(trackInfo.bitrate) + " kbps" : "Unknown";
            const juce::String sampleRateText = trackInfo.samplerate > 0 ? juce::String(trackInfo.samplerate) + " Hz" : "Unknown";
            const juce::String channelsText = trackInfo.channels > 0 ? juce::String(trackInfo.channels) : "Unknown";
            const juce::String codecText = trackInfo.codec_name.empty() ? "Unknown" : juce::String(trackInfo.codec_name);
            const juce::String titleText = trackInfo.title.empty() ? juce::String(trackInfo.filename) : juce::String(trackInfo.title);
            const juce::String artistText = trackInfo.artist_name.empty() ? "Unknown" : juce::String(trackInfo.artist_name);
            const juce::String albumText = trackInfo.album_title.empty() ? "Unknown" : juce::String(trackInfo.album_title);
            const juce::String bpmText = trackInfo.bpm.has_value() ? juce::String(static_cast<double>(*trackInfo.bpm) / 1000.0, 1) : "Unknown";
            const juce::String yearText = trackInfo.year > 0 ? juce::String(trackInfo.year) : "Unknown";

            juce::String details;
            details << "Title: " << titleText
                    << "\nArtist: " << artistText
                    << "\nAlbum: " << albumText
                    << "\nYear: " << yearText
                    << "\nTrack ID: " << juce::String(trackInfo.trackId)
                    << "\n\nFile: " << juce::String(trackInfo.filename)
                    << "\nPath: " << ui::jucePathFromFs(fullPath)
                    << "\n\nDuration: " << juce::String(durationToString(trackInfo.duration))
                    << "\nFormat: " << codecText
                    << "\nBitrate: " << bitrateText
                    << "\nSample Rate: " << sampleRateText
                    << "\nChannels: " << channelsText
                    << "\nBPM: " << bpmText
                    << "\nFile Size: " << juce::File::descriptionOfSizeInBytes(static_cast<juce::int64>(trackInfo.filesize_bytes));
            return details;
        }

    } // namespace

    namespace ui
    {
        MainViewType getLastKnownMainViewType()
        {
            return lastKnownViewType;
        }

        MainViewType determineMainViewType(const INavigationNode *node)
        {
            const auto nodePath{getNodePath(node)};
            if (nodePath.size() >= 3)
            {
                if (nodePath[1]->getName() == getMixesRootNodeName())
                {
                    lastKnownViewType = lastKnownViewTypeForMixes;
                    return lastKnownViewType;
                }
            }

            // Check if this is a MixNode anywhere in the tree (including under Exported)
            // MixNodes should always be able to show in MixEditor
            if (node)
            {
                // Try to cast to MixNode to check if this is a mix
                const auto* mixNode = dynamic_cast<const database::MixNode*>(node);
                if (mixNode)
                {
                    lastKnownViewType = lastKnownViewTypeForMixes;
                    return lastKnownViewType;
                }
            }

            // Add more types as needed
            lastKnownViewType = MainViewType::DataView; // Default to DataView if no specific type matches
            return lastKnownViewType;
        }

        MainComponent::MainComponent(juce::ApplicationCommandManager &commandManager)
            : MenuPresenter{commandManager},
              m_commandManager{commandManager},
              m_currentMainView{MainViewType::DataView},
              m_currentMainViewComponent{nullptr},  // Set after m_dataViewComponent is constructed
              m_dataViewComponent{*this},
              m_navigationPanel{*this},
              m_navigationTree{m_navigationPanel, m_dataViewComponent},
              m_verticalDivider{*this, true},
              m_playbackController{},
              m_enhancedPlayer{m_playbackController, m_audioFormatManager, m_audioThumbnailCache},
              m_statusPanel{*this},
              m_visualizerFIFO{config::theSettings.audioSettings.visualizerBufferSize}
        {
            // Initialize m_currentMainViewComponent now that m_dataViewComponent is constructed
            m_currentMainViewComponent = &m_dataViewComponent;

            // Load audio bypass states from config (inverted: bypassed = !enabled)
            m_equalizerEnabled = !config::theSettings.audioSettings.equalizerBypassed;
            m_reverbEnabled = !config::theSettings.audioSettings.reverbBypassed;

            // Load visualizer placement from config
            const int placementInt = config::theSettings.uiSettings.visualizerPlacement;
            m_visualizerPlacement = static_cast<config::VisualizerPlacement>(placementInt);

            // Set up visualizer
            m_visualizer.setVisualizerFIFO(&m_visualizerFIFO);
            m_visualizer.setPresetPath(ProjectMComponent::getDefaultPresetsDirectory());
            addChildComponent(m_visualizer);  // Not visible initially

            // Apply initial bypass states to the playback controller
            audio::model::EQSettings eqSettings;
            eqSettings.isActive = m_equalizerEnabled;
            m_playbackController.updateMasterEQ(eqSettings);

            audio::model::ReverbSettings reverbSettings;
            reverbSettings.isActive = m_reverbEnabled;
            m_playbackController.updateMasterReverb(reverbSettings);

            theThemeManager.applyCurrentTheme(m_lookAndFeel, this);

            // Register audio formats
            m_audioFormatManager.registerBasicFormats();

            // Set up state change callback
            m_playbackController.onStateChanged = [](PlaybackController::PlayerState state)
            {
                spdlog::info("[MainComponent] Player state changed to {}", static_cast<int>(state));
                // UI components will poll the state via timer
            };

            // Set up mix editor to use the unified playback controller
            m_mixEditorComponent.setPlaybackController(&m_playbackController);

            // Set up callback for navigation tree refresh when mix export status changes
            m_mixEditorComponent.setOnMixExportStatusChangedCallback([this]()
            {
                m_navigationTree.onMixExportStatusChanged();
            });
            m_mixEditorComponent.setOnMixSummaryChangedCallback([this]()
            {
                updateTrackCountStatus();
            });
            m_mixEditorComponent.setOnShowTrackInLibraryCallback([this](TrackId trackId)
            {
                showTrackInLibrary(trackId);
            });
            m_mixEditorComponent.setOnShowTrackDetailsCallback([this](TrackId trackId)
            {
                showTrackDetailsDialog(trackId);
            });
            // Point the genre cloud at whichever mix track is selected, so its album can be labelled.
            m_mixEditorComponent.getTimeline().onSelectedTrackChanged = [this](TrackId trackId)
            {
                m_statusPanel.getGenreCloud().setContextTrack(trackId);
            };
            // ...and follow playback too: whichever of the two moved last wins, which is what auditioning
            // through a mix actually looks like.
            m_mixEditorComponent.getTimeline().onPlayingTrackChanged = [this](TrackId trackId)
            {
                m_statusPanel.getGenreCloud().setContextTrack(trackId);
            };
            // Adding a genre can change how many rows the cloud needs, which changes the panel height.
            m_statusPanel.getGenreCloud().onLayoutNeeded = [this]()
            {
                resized();
            };
            m_statusPanel.getGenreCloud().onError = [this](const juce::String &message)
            {
                m_statusPanel.getStatusBar().postMessage(message, true);
            };

            // --- Add and make visible all child components ---
            addAndMakeVisible(m_dynamicToolbar);
            addAndMakeVisible(m_navigationPanel);
            addAndMakeVisible(m_verticalDivider);

            addAndMakeVisible(m_dataViewComponent);
            addChildComponent(m_mixEditorComponent); // also a child but not yet visible

            addAndMakeVisible(m_statusPanel);

            // --- Setup Callbacks from UI Components ---

            // Data View
            m_dataViewComponent.m_onRowActionRequested = [this](RowIndex_t rowIndex, DataAction action, const juce::Point<int> &screenPos)
            {
                handleRowActionFromDataView(rowIndex, action, screenPos);
            };

            // Dynamic Toolbar
            m_dynamicToolbar.m_onFilterTextChanged = [this](const juce::String &newFilterText)
            {
                handleFilterChange(newFilterText);
            };

            m_dynamicToolbar.m_onNodeActionClicked = [this](DataAction dataAction)
            {
                handleNodeActionFromToolbar(dataAction);
            };

            m_navigationPanel.m_onNodeSelected = [this](INavigationNode *selectedNode)
            {
                handleNodeSelection(selectedNode);
            };

            m_navigationPanel.m_onNodeAction = [this](INavigationNode *selectedNode, DataAction dataAction)
            {
                handleNodeActionFromNavigationPanel(selectedNode, dataAction);
            };

            // Enhanced Player callbacks
            m_enhancedPlayer.onMarkerAction = [this](TrackId trackId, std::chrono::milliseconds position, bool isNewMarker)
            {
                spdlog::info("Marker action requested: track={}, position={}ms, isNew={}", trackId, position.count(), isNewMarker);

                showMarkerDialog(trackId, position, isNewMarker);
            };

            m_enhancedPlayer.onNextTrack = [this]()
            {
                playNextTrack();
            };

            m_enhancedPlayer.onPreviousTrack = [this]()
            {
                playPreviousTrack();
            };

            // Track change callback for playlist mode - update UI when track changes
            m_playbackController.onCurrentTrackChanged = [this](TrackId trackId, [[maybe_unused]] size_t index)
            {
                // Update track highlight in DataView
                m_dataViewComponent.setPlayingTrackId(trackId);

                // Notify visualizer of track change (for preset switching)
                m_visualizer.onTrackChanged();

                // Update waveform and track info in EnhancedPlayer
                if (const auto* trackInfo = m_playbackController.getCurrentTrackInfo())
                {
                    const auto trackPath = trackInfo->reconstructFullPath();
                    juce::File audioFile(jucePathFromFs(trackPath));

                    if (audioFile.existsAsFile())
                    {
                        m_enhancedPlayer.loadFile(
                            audioFile,
                            std::format("{} / {} / {}", trackInfo->artist_name, trackInfo->album_title, trackInfo->title),
                            trackInfo->trackId
                        );

                        // Load markers for this track
                        const auto markers = theTrackLibrary.getMarkerManager().getMarkersForTrack(trackInfo->trackId);
                        m_enhancedPlayer.setMarkers(markers);
                    }
                }
            };

            if (!m_navigationTree.initialize())
            {
                // TODO: have a way to crash here?
                m_statusPanel.getStatusBar().postMessage("Error: Could not load navigation.", true);
            }

            // --- Playback Controller Setup ---
            // Listen to changes from the transport source (e.g., when a track
            // finishes)
            m_playbackController.getTransportSource().addChangeListener(this);
            // PlaybackController itself is a ChangeBroadcaster, if we need more
            // general state changes
            // m_playbackController.addChangeListener(this); // Already done by
            // old main component for transport

            // Initialize playback UI
            // UI will update via timer in EnhancedPlayerComponent
            // Initialize volume slider in toolbar from controller's current
            // gain (Assuming PlaybackController has a getGain() or
            // PlaybackToolbarComponent fetches it on init) For now, let
            // PlaybackController::syncUIToPlaybackControllerState handle this
            // via toolbar reference.

            // Connect visualizer FIFO to playback controller
            m_playbackController.setVisualizerFIFO(&m_visualizerFIFO);

            // Enable keyboard focus for media keys
            setWantsKeyboardFocus(true);

            // --- Application Commands and Menu ---
            m_commandManager.registerAllCommandsForTarget(this); // Register commands defined in this class

            auto &menuManager = getManager();

            // Helper lambda to create menu items with DataAction support
            auto makeActionItem = [&](const std::string &name,
                                      const std::string &desc,
                                      DataAction action,
                                      int key = 0,
                                      juce::ModifierKeys mods = juce::ModifierKeys::noModifiers) -> MenuItem
            {
                MenuItem item;
                item.name = name;
                item.description = desc;
                item.action = [&, action]()
                {
                    if (m_currentNode)
                    {
                        handleNodeActionFromNavigationPanel(m_currentNode, action);
                    }
                };
                if (key != 0)
                {
                    item.keyPress = MenuItem::KeyPress{static_cast<char>(key), mods};
                }
                item.isEnabled = [this, action]()
                {
                    return isActionAvailable(action);
                };
                return item;
            };

            // Helper for static menu items (always enabled)
            auto makeStaticItem = [](const std::string &name,
                                      const std::string &desc,
                                      std::function<void()> action,
                                      char key = 0,
                                      juce::ModifierKeys mods = juce::ModifierKeys::noModifiers,
                                      std::function<bool()> isEnabled = nullptr) -> MenuItem
            {
                MenuItem item;
                item.name = name;
                item.description = desc;
                item.action = action;
                item.isEnabled = isEnabled;
                if (key != 0)
                {
                    item.keyPress = MenuItem::KeyPress{key, mods};
                }
                return item;
            };

            // 1. Define File menu with enhanced options
            menuManager.registerMenu("File",
                {makeActionItem("New Working Set...",
                     "Create a new working set from current selection",
                     DataAction::CreateWorkingSet,
                     'n',
                     juce::ModifierKeys::commandModifier),
                    makeActionItem("New Mix...",
                        "Create a new mix from current selection",
                        DataAction::CreateMix,
                        'm',
                        juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier),
                    {"-"},
                    makeActionItem("Export Mix...", "Export the selected mix to audio file", DataAction::ExportMix, 'e', juce::ModifierKeys::commandModifier),
                    makeStaticItem("Export Scheduled Mixes...",
                        "Export all mixes scheduled for batch export",
                        [&]() { onBatchExport(); }),
                    {"-"},
                    makeStaticItem(
                        "Scan Folders...",
                        "Scan library folders for new tracks",
                        [&]()
                        {
                            onShowScanDialog();
                        },
                        's',
                        juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier),
                    makeStaticItem("Database Maintenance...",
                        "Perform database maintenance tasks",
                        [&]()
                        {
                            onShowMaintenanceDialog();
                        }),
                    {"-"},
                    makeStaticItem(
                        "Settings...",
                        "Open application settings",
                        [&]()
                        {
                            SettingsDialog::showSettingsDialog(this);
                        },
                        ',',
                        juce::ModifierKeys::commandModifier),
                    {"-"},
                    makeStaticItem(
                        "Exit",
                        "Quit JucyAudio",
                        [&]()
                        {
                            juce::JUCEApplication::getInstance()->systemRequestedQuit();
                        },
                        'q',
                        juce::ModifierKeys::commandModifier)});

            // 2. Define Edit menu with DataActions
            menuManager.registerMenu("Edit",
                {makeStaticItem("Undo",
                    "Undo last action",
                    [&]()
                    {
                        // Get current mix from mix editor
                        if (auto* mixNode = m_mixEditorComponent.getCurrentMixNode())
                        {
                            auto mixId = mixNode->getMixInfo().mixId;
                            if (theUndoManager.undo(mixId))
                            {
                                // Reload the mix to reflect changes
                                m_mixEditorComponent.loadMix(mixNode);
                            }
                        }
                    },
                    'z',
                    juce::ModifierKeys::commandModifier,
                    [&]()
                    {
                        // Enabled only if we're in mix editor and can undo
                        if (m_currentMainView == MainViewType::MixEditor)
                        {
                            if (auto* mixNode = m_mixEditorComponent.getCurrentMixNode())
                            {
                                return theUndoManager.canUndo(mixNode->getMixInfo().mixId);
                            }
                        }
                        return false;
                    }),
                    makeStaticItem("Redo",
                        "Redo last undone action",
                        [&]()
                        {
                            // Get current mix from mix editor
                            if (auto* mixNode = m_mixEditorComponent.getCurrentMixNode())
                            {
                                auto mixId = mixNode->getMixInfo().mixId;
                                if (theUndoManager.redo(mixId))
                                {
                                    // Reload the mix to reflect changes
                                    m_mixEditorComponent.loadMix(mixNode);
                                }
                            }
                        },
                        'y',
                        juce::ModifierKeys::commandModifier,
                        [&]()
                        {
                            // Enabled only if we're in mix editor and can redo
                            if (m_currentMainView == MainViewType::MixEditor)
                            {
                                if (auto* mixNode = m_mixEditorComponent.getCurrentMixNode())
                                {
                                    return theUndoManager.canRedo(mixNode->getMixInfo().mixId);
                                }
                            }
                            return false;
                        }),
                    {"-"},
                    makeActionItem("Delete", "Delete selected items", DataAction::Delete, juce::KeyPress::deleteKey, juce::ModifierKeys::noModifiers),
                    makeActionItem(
                        "Remove Tracks", "Remove selected tracks from working set", DataAction::RemoveTracks, 'r', juce::ModifierKeys::commandModifier),
                    {"-"},
                    makeActionItem("Edit Working Set Metadata...", "Edit metadata for selected working set", DataAction::EditWorkingSetMetadata),
                    makeActionItem("Edit Mix Metadata...", "Edit metadata for selected mix", DataAction::EditMixMetadata),
                    {"-"},
                    makeActionItem("Remove Duplicates",
                        "Remove duplicate tracks from selection",
                        DataAction::RemoveDuplicates,
                        'd',
                        juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier)});

            // 3. Define Library menu
            menuManager.registerMenu("Library",
                {makeActionItem("Play", "Play selected tracks", DataAction::Play, ' ', juce::ModifierKeys::noModifiers), // Spacebar
                    {"-"},
                    makeActionItem(
                        "Run BPM Analysis...", "Analyze BPM for selected tracks", DataAction::RunBpmAnalysis, 'b', juce::ModifierKeys::commandModifier),
                    makeActionItem("Show in Folder",
                        "Show selected track in system file browser",
                        DataAction::ShowInFolder,
                        'f',
                        juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier)});

            menuManager.registerMenu("View",
                {makeActionItem("Show Mix Editor", "Switch to mix editor view", DataAction::ShowMixEditor, '1', juce::ModifierKeys::commandModifier),
                    makeActionItem("Show Track Editor", "Switch to track editor view", DataAction::ShowTrackEditor, '2', juce::ModifierKeys::commandModifier),
                    {"-"},
                    makeStaticItem("Configure Columns...",
                        "Configure columns for the current view",
                        [&]()
                        {
                            onShowConfigureColumnsDialog();
                        }),
                    makeActionItem("Show Details", "Show detailed information", DataAction::ShowDetails, 'i', juce::ModifierKeys::commandModifier),
                    {"-"},
                    MenuItem{
                        "Toggle Visualizer",
                        "Show/hide music visualizer",
                        [this]()
                        {
                            toggleVisualizer();
                        },
                        MenuItem::KeyPress{'v', juce::ModifierKeys::commandModifier},
                        false, // not radio button
                        [this]()
                        {
                            return isVisualizerVisible();
                        }
                    },
                    MenuItem{
                        "Visualizer: Bottom",
                        "Place visualizer at bottom (full width)",
                        [this]() { setVisualizerPlacement(config::VisualizerPlacement::Bottom); },
                        std::nullopt,
                        true, // radio button
                        [this]() { return m_visualizerPlacement == config::VisualizerPlacement::Bottom; }
                    },
                    MenuItem{
                        "Visualizer: Left",
                        "Place visualizer on left side (full height)",
                        [this]() { setVisualizerPlacement(config::VisualizerPlacement::Left); },
                        std::nullopt,
                        true, // radio button
                        [this]() { return m_visualizerPlacement == config::VisualizerPlacement::Left; }
                    },
                    MenuItem{
                        "Visualizer: Right",
                        "Place visualizer on right side (full height)",
                        [this]() { setVisualizerPlacement(config::VisualizerPlacement::Right); },
                        std::nullopt,
                        true, // radio button
                        [this]() { return m_visualizerPlacement == config::VisualizerPlacement::Right; }
                    },
                    {"-"},
                    makeStaticItem(
                        "Refresh",
                        "Refresh current view",
                        [&]()
                        {
                            if (m_currentMainView == MainViewType::DataView)
                            {
                                m_dataViewComponent.refreshView();
                            }
                            else if (m_currentMainView == MainViewType::MixEditor)
                            {
                                // MixEditor refresh if needed
                            }
                        },
                        'r',
                        juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier),
                    {"-"},
                    // Theme picker dialog: Light/Dark columns with 16-colour swatches + live preview.
                    makeStaticItem("Theme...", "Choose a colour theme", [this]() { onShowThemeDialog(); })});

            menuManager.registerMenu("Tools",
                {
                    {"Scan VST3 Plugins...",
                        "Scan plugin folders for new or updated VST3 plugins",
                        [this]()
                        {
                            PluginScanDialog::launch(this);
                        }},
                    {"Master Effects...",
                        "Open the master effects chain editor",
                        [this]()
                        {
                            showMasterEffectsWindow();
                        }},
                    makeStaticItem(
                        "Bypass Master FX",
                        "Toggle global bypass for the master effects chain",
                        [this]()
                        {
                            toggleMasterEffectsBypass();
                        }),
                });

            menuManager.registerMenu("Help",
                {
                    {"About...",
                        "...",
                        [&]()
                        {
                            onShowAboutDialog();
                        }},
                });

            // 3. After defining everything, tell the presenter to register the commands with JUCE
            registerCommands();

#if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu(getMenuBarModel());
#endif
            // --- Timer ---
            startTimerHz(60); // Unified 60Hz base timer for TimerMultiplexer

            // Register VU meter updates with TimerMultiplexer
            // Update VU meter levels at 25Hz
            m_timerMultiplexer.registerClient(&m_statusPanel,
                25.0f,
                [this]()
                {
                    m_statusPanel.updateVUMeters();
                    m_statusPanel.getVUMeterLeft().updateDecay();
                    m_statusPanel.getVUMeterRight().updateDecay();
                });

            // Register EnhancedPlayer updates at 20Hz (was 50ms timer)
            m_timerMultiplexer.registerClient(&m_enhancedPlayer,
                20.0f,
                [this]()
                {
                    m_enhancedPlayer.updatePlaybackPosition();
                });

            // Register MixEditor playhead updates at 30Hz for better performance
            // This reduces rendering load during playback while maintaining smooth enough visuals
            m_timerMultiplexer.registerClient(&m_mixEditorComponent,
                30.0f,
                [this]()
                {
                    m_mixEditorComponent.updatePlayhead();
                });

            // Initial size
            setSize(1200, 800);

            // Required for AudioAppComponent
            setAudioChannels(0, 2); // Output only
            
            // Enable automatic device switching when the default device changes
            // deviceManager is inherited from AudioAppComponent
            deviceManager.addChangeListener(this);
            
            // On Windows, explicitly enable following the default device
            #if JUCE_WINDOWS
            auto setup = deviceManager.getAudioDeviceSetup();
            setup.useDefaultInputChannels = false;  // We don't use input
            setup.useDefaultOutputChannels = true;  // Follow default output device
            deviceManager.setAudioDeviceSetup(setup, true);
            #endif

            // Setup the background service
            theBackgroundTaskService.start();

            // Create and register our new BPM analysis task.
            // Note: the service will retain() the task, so we can release our initial reference.
            /* auto *bpmTask = new background_tasks::BpmAnalysis{};
            theBackgroundTaskService.registerTask(bpmTask);
            bpmTask->release(REFCOUNT_DEBUG_ARGS);
            */
        }

        MainComponent::~MainComponent()
        {
            // Stop and clean up visualizer
            m_visualizer.stop();

            // Remove device change listener before shutting down
            deviceManager.removeChangeListener(this);

            shutdownAudio();
            
            theBackgroundTaskService.stop();
#if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu(nullptr);
#endif

            stopTimer();

            // Sever callbacks that cross member lifetimes before any member is destroyed. Members die in
            // reverse declaration order, so m_statusPanel (declared after m_mixEditorComponent) goes first;
            // ~MixEditorComponent then unloads the mix, which clears the timeline selection and would fire
            // onSelectedTrackChanged into the already-destroyed panel.
            m_mixEditorComponent.getTimeline().onSelectedTrackChanged = nullptr;
            m_mixEditorComponent.getTimeline().onPlayingTrackChanged = nullptr;
            m_statusPanel.getGenreCloud().onLayoutNeeded = nullptr;
            m_statusPanel.getGenreCloud().onError = nullptr;

            // Unregister all components from timer multiplexer
            m_timerMultiplexer.unregisterComponent(&m_statusPanel);
            m_timerMultiplexer.unregisterComponent(&m_statusPanel.getVUMeterLeft());
            m_timerMultiplexer.unregisterComponent(&m_statusPanel.getVUMeterRight());
            m_timerMultiplexer.unregisterComponent(&m_enhancedPlayer);
            m_timerMultiplexer.unregisterComponent(&m_mixEditorComponent);

            // This is important for clean shutdown. It tells all child components
            // to stop using our m_lookAndFeel object before it gets destroyed.
            setLookAndFeel(nullptr);
            juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
            CheckboxLookAndFeel::releaseMemory();

            m_navigationPanel.releaseRootNode();
            m_navigationTree.releaseRootNode();

#ifdef USE_REFCOUNT_DEBUGGING
            for (const auto item : theBaseNodes)
            {
                spdlog::error("MainComponent::~MainComponent - BaseNode still retained: {} at {}", item->getName(), (void *)item);
            }
#endif

            shutdownAudio(); // From AudioAppComponent
            // Remove listeners
            if (juce::MessageManager::getInstanceWithoutCreating() != nullptr) // Check if MM still exists
            {
                m_playbackController.getTransportSource().removeChangeListener(this);
                // m_playbackController.removeChangeListener(this);
            }

            // Release retained INavigationNode pointers
            if (m_currentNode)
            {
                m_currentNode->dataNoLongerShowing(); // Good practice
                m_currentNode->release(REFCOUNT_DEBUG_ARGS);
                m_currentNode = nullptr;
            }
        }

        bool MainComponent::keyPressed(const juce::KeyPress &key)
        {
            const auto keyCode = key.getKeyCode();

// Windows media key virtual codes
#ifdef _WIN32
            constexpr int VK_MEDIA_PLAY_PAUSE = 0xB3;
            constexpr int VK_MEDIA_STOP = 0xB2;
            constexpr int VK_MEDIA_PREV_TRACK = 0xB1;
            constexpr int VK_MEDIA_NEXT_TRACK = 0xB0;
            constexpr int VK_VOLUME_MUTE = 0xAD;
            constexpr int VK_VOLUME_DOWN = 0xAE;
            constexpr int VK_VOLUME_UP = 0xAF;
#endif

            // Media keys (cross-platform)
            // Check both JUCE's cross-platform codes and Windows-specific virtual keys
            if (keyCode == juce::KeyPress::playKey || keyCode == juce::KeyPress::F8Key
#ifdef _WIN32
                || keyCode == VK_MEDIA_PLAY_PAUSE
#endif
            )
            {
                // Toggle play/pause
                if (m_playbackController.isPlaying())
                {
                    m_playbackController.pause();
                }
                else
                {
                    checkAndUpdateAudioDevice();
                    m_playbackController.play();
                }
                spdlog::info("[MainComponent] Media key: play/pause toggled");
                return true;
            }
            else if (keyCode == juce::KeyPress::stopKey
#ifdef _WIN32
                     || keyCode == VK_MEDIA_STOP
#endif
            )
            {
                // Stop playback
                m_playbackController.stop();
                spdlog::info("[MainComponent] Media key: stop");
                return true;
            }
            else if (keyCode == juce::KeyPress::fastForwardKey || keyCode == juce::KeyPress::F9Key
#ifdef _WIN32
                     || keyCode == VK_MEDIA_NEXT_TRACK
#endif
            )
            {
                // Next track
                playNextTrack();
                spdlog::info("[MainComponent] Media key: next track");
                return true;
            }
            else if (keyCode == juce::KeyPress::rewindKey || keyCode == juce::KeyPress::F7Key
#ifdef _WIN32
                     || keyCode == VK_MEDIA_PREV_TRACK
#endif
            )
            {
                // Previous track
                playPreviousTrack();
                spdlog::info("[MainComponent] Media key: previous track");
                return true;
            }
#ifdef _WIN32
            // Windows volume control keys
            else if (keyCode == VK_VOLUME_MUTE)
            {
                // Toggle mute (set gain to 0 or restore)
                static float previousGain = 1.0f;
                static bool isMuted = false;

                if (isMuted)
                {
                    m_playbackController.setGain(previousGain);
                    isMuted = false;
                    spdlog::info("[MainComponent] Volume unmuted (gain: {})", previousGain);
                }
                else
                {
                    previousGain = m_enhancedPlayer.getVolumeSliderValue();
                    m_playbackController.setGain(0.0f);
                    isMuted = true;
                    spdlog::info("[MainComponent] Volume muted");
                }
                return true;
            }
            else if (keyCode == VK_VOLUME_DOWN)
            {
                // Decrease volume by 10%
                const float currentGain = m_enhancedPlayer.getVolumeSliderValue();
                const float newGain = std::max(0.0f, currentGain - 0.1f);
                m_playbackController.setGain(newGain);
                m_enhancedPlayer.setVolumeSliderValue(newGain);
                spdlog::info("[MainComponent] Volume down: {}", newGain);
                return true;
            }
            else if (keyCode == VK_VOLUME_UP)
            {
                // Increase volume by 10%
                const float currentGain = m_enhancedPlayer.getVolumeSliderValue();
                const float newGain = std::min(1.0f, currentGain + 0.1f);
                m_playbackController.setGain(newGain);
                m_enhancedPlayer.setVolumeSliderValue(newGain);
                spdlog::info("[MainComponent] Volume up: {}", newGain);
                return true;
            }
#endif

            // Space bar for play/pause (common convention)
            if (keyCode == juce::KeyPress::spaceKey && !key.getModifiers().isAnyModifierKeyDown())
            {
                if (m_playbackController.isPlaying())
                {
                    m_playbackController.pause();
                }
                else
                {
                    checkAndUpdateAudioDevice();
                    m_playbackController.play();
                }
                spdlog::info("[MainComponent] Space key: play/pause toggled");
                return true;
            }

            // DEL key for deleting tracks from library
            if (keyCode == juce::KeyPress::deleteKey && !key.getModifiers().isAnyModifierKeyDown())
            {
                // Only trigger if we're in DataView with tracks selected
                if (m_currentMainView == MainViewType::DataView && m_currentNode)
                {
                    const auto selectedRows = m_dataViewComponent.getSelectedRowIndices();
                    if (!selectedRows.empty())
                    {
                        if (dynamic_cast<database::MixNode*>(m_currentNode) != nullptr ||
                            dynamic_cast<database::WorkingSetNode*>(m_currentNode) != nullptr)
                        {
                            onDataActionRemoveNamedObjects();
                        }
                        else
                        {
                            onDeleteTracksFromLibrary();
                        }
                        return true;
                    }
                }
            }

            return false; // Key not handled
        }

        void MainComponent::playNextTrack()
        {
            // If PlaybackController has a playlist, delegate to it
            if (m_playbackController.hasPlaylist())
            {
                m_playbackController.nextTrack();
                return;
            }

            if (m_currentMainView == MainViewType::DataView)
            {
                // In DataView mode - play next track in the current view (respects filters, sort order, etc.)
                const auto selectedRows = m_dataViewComponent.getSelectedRowIndices();
                const auto totalRows = m_dataViewComponent.getTotalRowCount();

                if (totalRows > 0)
                {
                    // Get the first selected row or default to -1 to start from beginning
                    const auto currentRow = selectedRows.empty() ? -1 : selectedRows.front();

                    int nextRow;
                    if (m_playbackController.getShuffleMode())
                    {
                        // Shuffle mode - pick a random track
                        std::random_device rd;
                        std::mt19937 gen(rd());
                        std::uniform_int_distribution<> dis(0, totalRows - 1);
                        nextRow = dis(gen);
                        spdlog::info("[MainComponent] Shuffle mode: picking random track {}", nextRow);
                    }
                    else
                    {
                        // Sequential mode
                        nextRow = currentRow + 1;

                        // Check if we're at the end of the playlist
                        if (nextRow >= totalRows)
                        {
                            if (m_playbackController.isRepeatEnabled())
                            {
                                // Repeat mode - wrap around to beginning
                                nextRow = 0;
                                spdlog::info("[MainComponent] Repeat mode: wrapping to beginning");
                            }
                            else
                            {
                                // No repeat - stop playback
                                spdlog::info("[MainComponent] End of playlist, stopping");
                                m_playbackController.stop();
                                return;
                            }
                        }
                    }

                    // Select and play the next row
                    m_dataViewComponent.selectSingleRow(nextRow);
                    playDataRow(nextRow);

                    spdlog::info("[MainComponent] Playing next track in DataView: row {}", nextRow);
                }
            }
            else if (m_currentMainView == MainViewType::MixEditor)
            {
                // In MixEditor mode - play next track in the mix
                if (m_playbackController.isMixMode())
                {
                    // Get current position and find which track is playing
                    const auto currentPos = m_playbackController.getCurrentPositionSeconds();

                    if (auto *mixNode = m_mixEditorComponent.getCurrentMixNode())
                    {
                        auto &mixLoader = mixNode->getMixProjectLoader();
                        const auto &mixTracks = mixLoader.getMixTracks();
                        if (!mixTracks.empty())
                        {
                            // Calculate track start times using the same algorithm as MixPlaybackEngine
                            std::vector<double> trackStartTimes;
                            trackStartTimes.reserve(mixTracks.size());

                            double previousAudioStartTime = 0.0;
                            for (size_t i = 0; i < mixTracks.size(); ++i)
                            {
                                const auto &mixTrack = mixTracks[i];
                                double audioStartTime;

                                if (i == 0)
                                {
                                    audioStartTime = 0.0;
                                }
                                else
                                {
                                    const auto &prevTrack = mixTracks[i - 1];
                                    audioStartTime = previousAudioStartTime + (prevTrack.attachTo.count() - mixTrack.attachFrom.count()) / 1000.0;
                                }

                                trackStartTimes.push_back(audioStartTime);
                                previousAudioStartTime = audioStartTime;
                            }

                            // Find the next track start time after current position
                            double nextTrackTime = -1.0;
                            bool foundNext = false;

                            for (const auto &startTime : trackStartTimes)
                            {
                                if (startTime > currentPos + 0.5) // Add small tolerance
                                {
                                    nextTrackTime = startTime;
                                    foundNext = true;
                                    break;
                                }
                            }

                            if (foundNext && nextTrackTime >= 0)
                            {
                                // Seek to the next track's start position
                                m_playbackController.seek(nextTrackTime);
                                spdlog::info("[MainComponent] Skipping to next track in mix at {}s", nextTrackTime);
                            }
                            else
                            {
                                // We're at the last track - wrap to beginning
                                m_playbackController.seek(0.0);
                                spdlog::info("[MainComponent] Wrapping to beginning of mix");
                            }
                        }
                    }
                }
                else
                {
                    spdlog::info("[MainComponent] No mix loaded in MixEditor mode");
                }
            }
        }

        void MainComponent::playPreviousTrack()
        {
            // If PlaybackController has a playlist, delegate to it
            if (m_playbackController.hasPlaylist())
            {
                m_playbackController.previousTrack();
                return;
            }

            if (m_currentMainView == MainViewType::DataView)
            {
                // In DataView mode - play previous track in the current view (respects filters, sort order, etc.)
                const auto selectedRows = m_dataViewComponent.getSelectedRowIndices();
                const auto totalRows = m_dataViewComponent.getTotalRowCount();

                if (totalRows > 0)
                {
                    int prevRow;
                    if (m_playbackController.getShuffleMode())
                    {
                        // Shuffle mode - pick a random track (going "back" in shuffle just picks another random)
                        std::random_device rd;
                        std::mt19937 gen(rd());
                        std::uniform_int_distribution<> dis(0, totalRows - 1);
                        prevRow = dis(gen);
                        spdlog::info("[MainComponent] Shuffle mode: picking random track {}", prevRow);
                    }
                    else
                    {
                        // Get the first selected row or default to 0
                        const auto currentRow = selectedRows.empty() ? 0 : selectedRows.front();

                        // Calculate previous row
                        if (currentRow > 0)
                        {
                            prevRow = currentRow - 1;
                        }
                        else if (m_playbackController.isRepeatEnabled())
                        {
                            // At beginning with repeat mode - wrap to end
                            prevRow = totalRows - 1;
                            spdlog::info("[MainComponent] Repeat mode: wrapping to end");
                        }
                        else
                        {
                            // At beginning without repeat - stay at beginning
                            prevRow = 0;
                        }
                    }

                    // Select and play the previous row
                    m_dataViewComponent.selectSingleRow(prevRow);
                    playDataRow(prevRow);

                    spdlog::info("[MainComponent] Playing previous track in DataView: row {}", prevRow);
                }
            }
            else if (m_currentMainView == MainViewType::MixEditor)
            {
                // In MixEditor mode - play previous track in the mix
                if (m_playbackController.isMixMode())
                {
                    // Get current position and find which track is playing
                    const auto currentPos = m_playbackController.getCurrentPositionSeconds();

                    if (auto *mixNode = m_mixEditorComponent.getCurrentMixNode())
                    {
                        auto &mixLoader = mixNode->getMixProjectLoader();
                        const auto &mixTracks = mixLoader.getMixTracks();
                        if (!mixTracks.empty())
                        {
                            // Calculate track start times using the same algorithm as MixPlaybackEngine
                            std::vector<double> trackStartTimes;
                            trackStartTimes.reserve(mixTracks.size());

                            double previousAudioStartTime = 0.0;
                            for (size_t i = 0; i < mixTracks.size(); ++i)
                            {
                                const auto &mixTrack = mixTracks[i];
                                double audioStartTime;

                                if (i == 0)
                                {
                                    audioStartTime = 0.0;
                                }
                                else
                                {
                                    const auto &prevTrack = mixTracks[i - 1];
                                    audioStartTime = previousAudioStartTime + (prevTrack.attachTo.count() - mixTrack.attachFrom.count()) / 1000.0;
                                }

                                trackStartTimes.push_back(audioStartTime);
                                previousAudioStartTime = audioStartTime;
                            }

                            // Find the previous track start time before current position
                            double prevTrackTime = 0.0;

                            for (auto it = trackStartTimes.rbegin(); it != trackStartTimes.rend(); ++it)
                            {
                                if (*it < currentPos - 1.0) // Subtract tolerance to avoid getting stuck
                                {
                                    prevTrackTime = *it;
                                    break;
                                }
                            }

                            // If we're very close to the start of current track, go to previous track
                            // Otherwise, restart current track
                            bool shouldGoToPrevious = false;
                            for (const auto &startTime : trackStartTimes)
                            {
                                if (std::abs(startTime - currentPos) < 3.0) // Within 3 seconds of track start
                                {
                                    shouldGoToPrevious = true;
                                    break;
                                }
                            }

                            if (shouldGoToPrevious || currentPos < 3.0)
                            {
                                // Go to actual previous track
                                m_playbackController.seek(prevTrackTime);
                                spdlog::info("[MainComponent] Going to previous track in mix at {}s", prevTrackTime);
                            }
                            else
                            {
                                // Restart current track - find the most recent track start
                                for (auto it = trackStartTimes.rbegin(); it != trackStartTimes.rend(); ++it)
                                {
                                    if (*it <= currentPos)
                                    {
                                        m_playbackController.seek(*it);
                                        spdlog::info("[MainComponent] Restarting current track in mix at {}s", *it);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                else
                {
                    spdlog::info("[MainComponent] No mix loaded in MixEditor mode");
                }
            }
        }

        void MainComponent::paint(juce::Graphics &g)
        {
            // Optionally fill the main background if child components don't
            // cover everything or if there are gaps.
            const auto backgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
            // spdlog::info("MainComponent::paint - juce::ResizableWindow::backgroundColourId colour: '#{}'", backgroundColour.toString().toStdString());
            g.fillAll(backgroundColour);
        }

        // Method called by DividerComponent's mouseDrag
        void MainComponent::updateNavPanelWidthFromDrag(int originalNavPanelWidthAtDragStart, int dragDeltaX)
        {
            int newNavPanelWidth = originalNavPanelWidthAtDragStart + dragDeltaX;

            // Get the bounds of the area available for navPanel, divider, and
            // dataView auto bounds = getLocalBounds();
            /* int toolbarHeight = */ m_dynamicToolbar.getHeight();  // Assuming toolbar is visible and has a height
            /* int bottomPanelHeight = */ m_statusPanel.getHeight(); // Assuming panel is visible

            // Adjust bounds for toolbar and bottom panel if they are part of
            // MainComponent's layout If they are laid out *within*
            // MainComponent's getLocalBounds():
            // bounds.removeFromTop(toolbarHeight);
            // bounds.removeFromBottom(bottomPanelHeight);
            // For now, assume centralArea in resized() gives us the correct
            // reference width. We need the width of the area that the navPanel
            // and dataView share.

            // Let's use the same logic as in resized() to get the available
            // central width
            auto tempBounds = getLocalBounds();
            tempBounds.removeFromTop(m_dynamicToolbar.getHeight());
            tempBounds.removeFromBottom(m_statusPanel.getHeight());
            int availableWidthForSplitPanes = tempBounds.getWidth();

            // Apply constraints
            int minNavWidth = 100;  // Minimum width for navigation panel
            int minDataWidth = 200; // Minimum width for data view

            // Maximum navigation panel width is the total available space minus
            // divider and min data view width
            int maxNavWidth = availableWidthForSplitPanes - m_dividerThickness - minDataWidth;
            if (maxNavWidth < minNavWidth) // Handle case where window is too small
            {
                maxNavWidth = minNavWidth; // Allow nav panel to be its min,
                                           // data view might get squeezed
                if (availableWidthForSplitPanes - m_dividerThickness - minNavWidth < 0)
                { // Not even space for minNav and divider
                  // degenerate case, perhaps set navPanel to a very small fixed
                  // value or hide data view
                }
            }

            m_navPanelWidth = juce::jlimit(minNavWidth, maxNavWidth, newNavPanelWidth);

            resized(); // Trigger re-layout of all components with the new
                       // m_navPanelWidth
        }

        void MainComponent::resized()
        {
            // Track resize frequency
            static int resizeCallCount = 0;
            static auto lastReportTime = std::chrono::high_resolution_clock::now();
            resizeCallCount++;
            
            const auto startTime = std::chrono::high_resolution_clock::now();
            
            // Report frequency every second
            auto now = std::chrono::high_resolution_clock::now();
            auto timeSinceReport = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReportTime);
            if (timeSinceReport.count() >= 1000)
            {
                spdlog::debug("MainComponent::resized called {} times in last second", resizeCallCount);
                resizeCallCount = 0;
                lastReportTime = now;
            }
            
            auto bounds = getLocalBounds();
            // Get actual heights after components are potentially laid out once
            int toolbarHeight = m_dynamicToolbar.isVisible() ? m_dynamicToolbar.getHeight() : 0;
            if (toolbarHeight == 0 && m_dynamicToolbar.isVisible())
                toolbarHeight = 40; // Default if not yet sized

            int bottomPanelHeight = m_statusPanel.isVisible() ? m_statusPanel.getPreferredHeight(bounds.getWidth()) : 0;

            m_dynamicToolbar.setBounds(bounds.removeFromTop(toolbarHeight));
            m_statusPanel.setBounds(bounds.removeFromBottom(bottomPanelHeight));

            // Handle visualizer placement if visible
            constexpr int visualizerHeight = 300; // Height for bottom placement
            constexpr int visualizerWidth = 400;  // Width for left/right placement

            if (m_visualizerVisible)
            {
                if (m_visualizerPlacement == config::VisualizerPlacement::Bottom)
                {
                    // Bottom: full width, fixed height, above status panel
                    auto visualizerArea = bounds.removeFromBottom(visualizerHeight);
                    m_visualizer.setBounds(visualizerArea);
                }
                // Left and Right are handled below in the central area layout
            }

            auto centralArea = bounds; // This is now the area for nav, divider, data (and possibly visualizer)

            // Handle Left and Right visualizer placements
            if (m_visualizerVisible)
            {
                if (m_visualizerPlacement == config::VisualizerPlacement::Left)
                {
                    // Left: visualizer on the left side, full height
                    auto visualizerArea = centralArea.removeFromLeft(visualizerWidth);
                    m_visualizer.setBounds(visualizerArea);
                }
                else if (m_visualizerPlacement == config::VisualizerPlacement::Right)
                {
                    // Right: visualizer on the right side, full height
                    auto visualizerArea = centralArea.removeFromRight(visualizerWidth);
                    m_visualizer.setBounds(visualizerArea);
                }
            }

            // Constraints are now mostly handled in
            // updateNavPanelWidthFromDrag, but resized() should still respect
            // them for initial layout or window resize.
            int minNavWidth = 100;
            int minDataWidth = 200;

            int currentNavWidth = m_navPanelWidth; // Use the member variable

            // Ensure nav panel width is valid given current centralArea width
            int maxNavWidthForCurrentSize = centralArea.getWidth() - m_dividerThickness - minDataWidth;
            if (maxNavWidthForCurrentSize < minNavWidth)
                maxNavWidthForCurrentSize = minNavWidth;

            currentNavWidth = juce::jlimit(minNavWidth, maxNavWidthForCurrentSize, currentNavWidth);
            if (currentNavWidth != m_navPanelWidth)
            { // If window resize forced a change
                m_navPanelWidth = currentNavWidth;
            }

            // Layout based on m_navPanelWidth (which might have been adjusted)
            m_navigationPanel.setBounds(centralArea.getX(), centralArea.getY(), m_navPanelWidth, centralArea.getHeight());

            m_verticalDivider.setBounds(centralArea.getX() + m_navPanelWidth, centralArea.getY(), m_dividerThickness, centralArea.getHeight());

            int dataViewX = centralArea.getX() + m_navPanelWidth + m_dividerThickness;
            int dataViewWidth = centralArea.getWidth() - m_navPanelWidth - m_dividerThickness;
            if (dataViewWidth < 0)
                dataViewWidth = 0; // Prevent negative width

            // m_currentMainViewComponent->setBounds(dataViewX, centralArea.getY(), dataViewWidth, centralArea.getHeight());
            m_dataViewComponent.setBounds(dataViewX, centralArea.getY(), dataViewWidth, centralArea.getHeight());
            
            const auto mixEditorStart = std::chrono::high_resolution_clock::now();
            m_mixEditorComponent.setBounds(dataViewX, centralArea.getY(), dataViewWidth, centralArea.getHeight());
            const auto mixEditorEnd = std::chrono::high_resolution_clock::now();
            
            const auto endTime = std::chrono::high_resolution_clock::now();
            const auto mixEditorDuration = std::chrono::duration_cast<std::chrono::microseconds>(mixEditorEnd - mixEditorStart);
            const auto totalDuration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
            
            spdlog::debug("MainComponent::resized Performance:");
            spdlog::debug("  MixEditor.setBounds: {} µs", mixEditorDuration.count());
            spdlog::debug("  Total: {} µs ({} ms)", totalDuration.count(), totalDuration.count() / 1000);
        }

        void MainComponent::adjustSplitterPosition([[maybe_unused]] int desiredNewNavPanelLeftEdge) // Or pass delta
        {
            // For Step 2:
            // int newNavPanelWidth = desiredNewNavPanelLeftEdge -
            // getLocalBounds().getX(); // Assuming centralArea starts at getX()
            // int centralX = m_dynamicToolbar.getBounds().getBottom(); // Y of
            // central area start int centralAreaXOffset =
            // m_navigationPanel.getBounds().getX(); // X where nav panel starts
            // newNavPanelWidth = desiredNewNavPanelLeftEdge -
            // centralAreaXOffset;

            // // Apply constraints
            // int minNavWidth = 100;
            // int minDataWidth = 200;
            // auto centralAreaWidth = getLocalBounds().getWidth(); //
            // Simplified, assuming no side margins for centralArea if
            // (m_dynamicToolbar.isVisible()) centralAreaWidth = getWidth(); //
            // Use full component width as reference for central area calc

            // int maxNavWidth = centralAreaWidth - m_dividerThickness -
            // minDataWidth; if (maxNavWidth < minNavWidth) maxNavWidth =
            // minNavWidth;

            // m_navPanelWidth = juce::jlimit(minNavWidth, maxNavWidth,
            // newNavPanelWidth); resized(); // Trigger re-layout
        }

        // --- juce::AudioAppComponent Overrides ---
        void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
        {
            // Log device information when prepareToPlay is called
            if (auto* currentDevice = deviceManager.getCurrentAudioDevice())
            {
                const auto deviceName = currentDevice->getName();
                spdlog::info("PrepareToPlay called - Device: {}, Sample Rate: {}, Block Size: {}", 
                            deviceName.toStdString(), sampleRate, samplesPerBlockExpected);
                
                m_statusPanel.getStatusBar().postMessage(
                    "Audio device: " + deviceName + juce::String::formatted(" (%.0f Hz)", sampleRate),
                    false);
            }
            else
            {
                spdlog::info("PrepareToPlay called - Sample Rate: {}, Block Size: {}", 
                            sampleRate, samplesPerBlockExpected);
                m_statusPanel.getStatusBar().postMessage("Audio device prepared.", false);
            }
            
            m_playbackController.prepareToPlay(samplesPerBlockExpected, sampleRate);
        }

        void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill)
        {
            m_playbackController.getNextAudioBlock(bufferToFill);
            m_playbackController.processAudioBlock(*bufferToFill.buffer);
        }

        void MainComponent::releaseResources()
        {
            m_playbackController.releaseResources();
            m_statusPanel.getStatusBar().postMessage("Audio resources released.", false);
        }
        
        void MainComponent::checkAndUpdateAudioDevice()
        {
            // Check if the current audio device setup needs updating
            auto currentSetup = deviceManager.getAudioDeviceSetup();
            
            // Get the current default output device
            auto* deviceType = deviceManager.getCurrentDeviceTypeObject();
            if (deviceType)
            {
                const auto defaultOutputDevice = deviceType->getDefaultDeviceIndex(false);
                const auto deviceNames = deviceType->getDeviceNames(false);
                
                if (defaultOutputDevice >= 0 && defaultOutputDevice < deviceNames.size())
                {
                    const auto defaultDeviceName = deviceNames[defaultOutputDevice];
                    
                    // Check if we need to switch to the default device
                    if (currentSetup.outputDeviceName != defaultDeviceName)
                    {
                        spdlog::info("Switching to default audio device: {} (was: {})", 
                                    defaultDeviceName.toStdString(),
                                    currentSetup.outputDeviceName.toStdString());
                        
                        // Update to use the default device
                        currentSetup.outputDeviceName = defaultDeviceName;
                        currentSetup.useDefaultOutputChannels = true;
                        
                        // Apply the new setup
                        auto result = deviceManager.setAudioDeviceSetup(currentSetup, true);
                        if (result.isEmpty())
                        {
                            m_statusPanel.getStatusBar().postMessage(
                                "Switched to: " + defaultDeviceName,
                                false);
                        }
                        else
                        {
                            spdlog::error("Failed to switch audio device: {}", result.toStdString());
                        }
                    }
                }
            }
        }

        // --- juce::Timer Override ---
        void MainComponent::timerCallback()
        {
            // Tick the timer multiplexer at 60Hz
            m_timerMultiplexer.tick();

            // Check for audio device changes periodically (every ~1 second at 60Hz)
            static int deviceCheckCounter = 0;
            if (++deviceCheckCounter >= 60)
            {
                deviceCheckCounter = 0;
                
                // Only check if we're playing
                if (m_playbackController.isPlaying())
                {
                    checkAndUpdateAudioDevice();
                }
            }
        }

        // --- juce::ChangeListener Override ---
        void MainComponent::changeListenerCallback(juce::ChangeBroadcaster *source)
        {
            if (source == &m_playbackController.getTransportSource())
            {
                // Check if the track has finished playing
                if (!m_playbackController.getTransportSource().isPlaying() && m_playbackController.getState() == PlaybackController::PlayerState::TrackPlaying)
                {
                    // Track just ended - auto-advance to next track
                    spdlog::info("[MainComponent] Track ended, auto-advancing to next");
                    playNextTrack();
                }
            }
            else if (source == &deviceManager)
            {
                // Audio device has changed
                spdlog::info("[MainComponent] Audio device change detected");
                
                // The AudioAppComponent will automatically handle the device switch,
                // but we can add any additional handling here if needed
                
                if (auto* currentDevice = deviceManager.getCurrentAudioDevice())
                {
                    const auto deviceName = currentDevice->getName();
                    spdlog::info("Switched to audio device: {}", deviceName.toStdString());
                    m_statusPanel.getStatusBar().postMessage(
                        "Audio device: " + deviceName,
                        false);
                }
            }
            // else if (source == &m_playbackController) { /* Handle other
            // general PlaybackController changes */ }
        }

        // --- UI State Synchronization ---
        void MainComponent::syncPlaybackUIToControllerState()
        {
            // No longer needed - EnhancedPlayerComponent handles UI updates via its timer
        }

        // --- Handler Method Stubs / Basic Logic ---
        void MainComponent::handleNodeSelection(
            INavigationNode *selectedNode, bool forceDisplaySwitch, bool syncNavigationTree) // selectedNode is retained by caller (NavPanel)
        {
            const auto start{std::chrono::high_resolution_clock::now()};
            const auto currentViewType{m_currentMainView};
            if (forceDisplaySwitch)
            {
                // don't change node:
                assert(selectedNode == nullptr);
            }
            else
            {
                if (m_currentNode == selectedNode)
                {
                    if (selectedNode)
                        selectedNode->release(REFCOUNT_DEBUG_ARGS); // Release the new one if it's same as old
                    return;
                }

                if (m_currentNode)
                {
                    m_currentNode->dataNoLongerShowing();
                    m_currentNode->release(REFCOUNT_DEBUG_ARGS);
                }
                m_currentNode = selectedNode; // Takes ownership of the retained selectedNode

                // Sync the navigation tree if requested (e.g., when navigating from data view)
                if (syncNavigationTree && selectedNode)
                {
                    spdlog::info("Syncing navigation tree to node: {}", selectedNode->getName());
                    m_navigationPanel.selectNode(selectedNode);
                }
            }

            if (m_currentNode)
            {
                const auto newViewType{determineMainViewType(m_currentNode)};
                if (currentViewType != newViewType)
                {
                    m_mixEditorComponent.unloadMix(); // Clear previous mix data
                    m_currentMainViewComponent->setVisible(false);
                    if (newViewType == MainViewType::MixEditor)
                    {
                        m_currentMainViewComponent = &m_mixEditorComponent;
                        m_currentMainView = MainViewType::MixEditor;
                        m_mixEditorComponent.setVisible(true);
                        // Hide the bottom-bar waveform: in the mix editor it would show whatever track the
                        // standalone player last loaded, which is rarely the track being edited or played
                        // back from the mix — confusing rather than informative.
                        m_enhancedPlayer.setWaveformVisible(false);
                        // The strip the waveform vacated is where the genre cloud goes.
                        m_statusPanel.setGenreCloudVisible(true);
                        resized();
                    }
                    else
                    {
                        m_currentMainViewComponent = &m_dataViewComponent;
                        m_currentMainView = MainViewType::DataView;
                        m_dataViewComponent.setVisible(true);
                        // Arriving in DataView from a different view (typically MixEditor): any preserved row selection
                        // is stale, because the node's tracks may have changed while we were away (e.g. a working set
                        // loses tracks after mix export). Row indices would otherwise highlight different songs.
                        m_dataViewComponent.clearSelection();
                        m_enhancedPlayer.setWaveformVisible(true);
                        m_statusPanel.setGenreCloudVisible(false);
                        resized();
                    }
                }

                // we should use a function to crate a string path here.
                const auto path{getNodePath(m_currentNode)};

                m_currentNode->prepareToShowData();
                m_dynamicToolbar.setCurrentNode(m_currentNode); // Toolbar updates its actions

                // Apply current global filter to the new node
                // Get the current filter text from the toolbar
                const auto currentFilterText = m_dynamicToolbar.getFilterText();
                std::vector<std::string> searchTerms;
                if (!currentFilterText.isEmpty())
                {
                    searchTerms.push_back(currentFilterText.toStdString());
                }
                // Always set search terms (even if empty) to clear any previous filter on the node
                m_currentNode->setSearchTerms(searchTerms);

                if (m_currentMainView == MainViewType::MixEditor)
                {
                    m_mixEditorComponent.loadMix(static_cast<MixNode *>(m_currentNode)); // Load the mix data

                    // MixEditorComponent now uses PlaybackController directly - no callbacks needed
                }
                else
                {
                    m_dataViewComponent.setCurrentNode(m_currentNode); // DataView updates its content source
                    m_dataViewComponent.refreshView();                 // Tell DataView to redraw
                }
                updateTrackCountStatus();
            }
            else
            {
                m_dynamicToolbar.setCurrentNode(nullptr);
                if (m_currentMainView == MainViewType::MixEditor)
                {
                    // in MixEditor view, there is no "current node" concept, so we can ignore this.
                }
                else
                {
                    m_dataViewComponent.setCurrentNode(nullptr);
                    m_dataViewComponent.refreshView();
                }
                updateTrackCountStatus();
            }
            // UI will update via timer in EnhancedPlayerComponent // Update play button enable
            // state
            const auto end{std::chrono::high_resolution_clock::now()};
            const auto duration{std::chrono::duration_cast<std::chrono::milliseconds>(end - start)};
            spdlog::info("MainComponent::handleNodeSelection took {} ms", duration.count());
        }

        void MainComponent::removeTrackFromMix(TrackId trackId)
        {
            if (!m_currentNode)
                return;

            MixId mixId = m_currentNode->getUniqueId();
            spdlog::info("Soft-deleting track {} from mix {}", trackId, mixId);

            auto *mixManager = &theTrackLibrary.getMixManager();
            if (mixManager->removeTrackFromMix(mixId, trackId))
            {
                m_statusPanel.getStatusBar().postMessage("Track removed from mix.", false);
                // Refresh the mix editor to show the change
                m_mixEditorComponent.loadMix(static_cast<MixNode *>(m_currentNode));
            }
            else
            {
                m_statusPanel.getStatusBar().postMessage("Failed to remove track from mix.", true);
                spdlog::error("Failed to soft-delete track {} from mix {}", trackId, mixId);
            }
        }

        void MainComponent::seekToTimelinePosition(double timePosition)
        {
            spdlog::info("seekToTimelinePosition called with time: {:.2f}", timePosition);
            spdlog::info("Current playback state: {}", static_cast<int>(m_playbackController.getState()));

            if (m_playbackController.isPlaying())
            {
                spdlog::info("Something is playing - switching to clicked track position");

                // We need to figure out which track was clicked and play that
                // For now, let's get the selected track and play it from the position
                auto &timeline = m_mixEditorComponent.getTimeline(); // You'll need to add this getter
                if (timeline.getSelectedTrack())
                {
                    // Play the selected track from the clicked position
                    timeline.playSelectedTrackFromPosition(timePosition);
                }
                else
                {
                    // Fallback: just seek within current track
                    m_playbackController.seek(timePosition);
                }
            }
            else
            {
                spdlog::info("Nothing playing - just updating visual position (no playback change)");
                // Just update the visual position - no playback starts
                m_statusPanel.getStatusBar().postMessage("Position set to " + juce::String(timePosition, 1) + "s", false);
            }
        }

        void MainComponent::handleFilterChange(const juce::String &newFilterText)
        {
            if (m_currentNode)
            {
                // For FTS5, we pass the entire search string as a single term.
                // The FTS engine will handle parsing quotes, AND/OR logic, etc.
                std::vector<std::string> searchTerms;
                if (!newFilterText.isEmpty())
                {
                    searchTerms.push_back(newFilterText.toStdString());
                }

                if (m_currentNode->setSearchTerms(searchTerms))
                {
                    if (m_currentMainView == MainViewType::MixEditor)
                    {
                        // mix editor doesn't support filtering, so nothing to see here
                    }
                    else
                    {
                        m_dataViewComponent.refreshView(); // Tell DataView data has changed due to filter
                        updateTrackCountStatus();

                        // Check if we're in the Folders view by checking the node type
                        const bool isInFoldersView =
                            dynamic_cast<database::VirtualFoldersOverview*>(m_currentNode) != nullptr ||
                            dynamic_cast<database::VirtualFolderNode*>(m_currentNode) != nullptr;

                        if (isInFoldersView)
                        {
                            // Update the shared folder filter state in TrackLibrary
                            theTrackLibrary.setFolderFilter(searchTerms);

                            // Trigger navigation panel repaint so all folder nodes pick up the new state
                            m_navigationPanel.getTreeView().repaint();
                        }
                    }
                }
            }
        }

        void MainComponent::handleNodeActionFromToolbar(DataAction action)
        {
            if (!m_currentNode)
                return;
            handleNodeActionFromNavigationPanel(m_currentNode, action);
        }

        void MainComponent::handleNodeActionFromNavigationPanel(INavigationNode *node, DataAction action)
        {
            m_statusPanel.getStatusBar().postMessage("Node action: " + juce::String(dataActionToString(action, m_currentNode)), false);

            switch (action)
            {
            case DataAction::CreateWorkingSet:
                createWorkingSet();
                break;
            case DataAction::RemoveDuplicates:
                onRemoveDuplicates(m_currentNode);
                break;
            case DataAction::CreateMix:
                createMix();
                break;
            case DataAction::Delete:
                onDataActionDelete(m_currentNode);
                break;
            case DataAction::ExportMix:
                onExportMix(node);
                break;
            case DataAction::EditWorkingSetMetadata:
                onEditWorkingSetMetadata(node);
                break;
            case DataAction::EditMixMetadata:
                onEditMixMetadata(node);
                break;
            case DataAction::RunBpmAnalysis:
                onRunBpmAnalysis(node);
                break;
            case DataAction::ShowMixEditor:
                onShowMixEditor();
                break;
            case DataAction::ShowTrackEditor:
                onShowTrackEditor();
                break;
            case DataAction::Settings:
                SettingsDialog::showSettingsDialog(this);
                break;
            case DataAction::ScanFolders:
                onShowScanDialog();
                break;
            case DataAction::ShowEqualizer:
                toggleEqualizerWindow();
                break;
            case DataAction::ShowReverb:
                toggleReverbWindow();
                break;
            case DataAction::ShowMasterEffects:
                showMasterEffectsWindow();
                break;
            case DataAction::ToggleMasterEffectsBypass:
                toggleMasterEffectsBypass();
                break;
            case DataAction::UnlockMixForEditing:
                onUnlockMixForEditing(node);
                break;
            default:
                spdlog::error("Unsupported action '{}' for node '{}' in MainComponent::handleNodeActionFromNavigationPanel. This should not happen.",
                    dataActionToString(action, m_currentNode).toStdString(),
                    node->getName());
                break;
            }
        }

        void MainComponent::handleRowActionFromDataView(RowIndex_t rowIndex, DataAction action, const juce::Point<int> & /*screenPos*/)
        {
            switch (action)
            {
            case DataAction::Play:
                playDataRow(rowIndex);
                break;
            case DataAction::CreateWorkingSet:
                createWorkingSet();
                break;
            case DataAction::CreateMix:
                createMix();
                break;
            case DataAction::Delete:
                onDataActionRemoveNamedObjects(); //-V1037
                break;
            case DataAction::RunBpmAnalysis:
                onRunBpmAnalysisForSelectedRows();
                break;
            case DataAction::CheckFilesOnDisk:
                onCheckFilesForSelectedRows();
                break;
            case DataAction::ShowDetails:
            {
                if (!m_currentNode)
                    break;
                auto rows = m_dataViewComponent.getSelectedRowIndices();
                if (rows.empty())
                    rows = {rowIndex};
                launchTrackDetailsDialog(m_currentNode->getTrackInfosForOperation(rows).trackInfos);
                break;
            }
            case DataAction::ShowInFolder:
                onShowInFolder(rowIndex);
                break;
            case DataAction::CopyToClipboard:
                onCopyToClipboard(rowIndex);
                break;
            case DataAction::RemoveTracks: // TODO: we should do this only from the data View
                onDataActionRemoveNamedObjects();
                break;
            case DataAction::DeleteTracksFromLibrary:
                onDeleteTracksFromLibrary();
                break;
            case DataAction::None:
            default:
                break;
            }
        }

        void MainComponent::onRunBpmAnalysis(INavigationNode *node)
        {
            if (!node)
                return;

            // Use the Node-Centric method to get all track IDs
            const auto trackResult = node->getAllTrackInfosForOperation();
            if (trackResult.trackInfos.empty())
            {
                m_statusPanel.getStatusBar().postMessage("No tracks to analyze.", true);
                return;
            }

            auto *task = new background_tasks::BpmAnalysisTask{std::move(trackResult.trackInfos)};
            TaskDialog::launch("BPM Analysis",
                task,
                TaskDialog::AutoCloseMode::NoAutoClose, 500, this,
                [this, task](bool /*success*/)
                {
                    m_dataViewComponent.refreshView();

                    // Check for bad files
                    const auto &badFiles = task->getBadFiles();
                    if (!badFiles.empty())
                    {
                        showBadFilesDialog(badFiles);
                    }
                });
            task->release(REFCOUNT_DEBUG_ARGS);
        }

        void MainComponent::onRemoveDuplicates(INavigationNode *node)
        {
            if (!node)
                return;

            const auto workingSetId = node->getWorkingSetId();
            if (workingSetId <= 0)
            {
                m_statusPanel.getStatusBar().postMessage("No working set to analyze.", true);
                return;
            }

            class RemoveDuplicatesFromWorkingSet : public ILongRunningTask
            {
            public:
                RemoveDuplicatesFromWorkingSet(WorkingSetId workingSetId)
                    : ILongRunningTask{"Remove duplicates from working set", true},
                      m_workingSetId{workingSetId}
                {
                }

            private:
                ~RemoveDuplicatesFromWorkingSet() override
                {
                }

                static std::string createUniqueTrackKey(const TrackInfo &ti)
                {
                    return std::format("{}/{}/{}/{}/{}", ti.artist_name, ti.album_title, ti.title, ti.bpm.has_value() ? *ti.bpm : 0, ti.duration);
                }

                void run(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel) override
                {
                    // Step 1: Finalize the mix (prune working set, update status)
                    progressCb(-1, "Removing tracks from working set");
                    if (shouldCancel)
                    {
                        completionCb(false, "Cancelled before finalization.");
                        return;
                    }

                    TrackQueryArgs tqa{};
                    tqa.workingSetId = m_workingSetId;
                    tqa.usePaging = false;
                    const auto allTracks{theTrackLibrary.getTrackDatabase().getTracks(tqa)};
                    spdlog::info("Found a total of {} tracks in working set {}", allTracks.size(), m_workingSetId);

                    std::vector<TrackId> trackIdsToRemove;

                    std::unordered_set<std::string> uniqueTrackKeys;
                    for (const auto &track : allTracks)
                    {
                        const auto uniqueTrackKey{createUniqueTrackKey(track)};
                        if (uniqueTrackKeys.contains(uniqueTrackKey))
                        {
                            // Duplicate found, remove it
                            trackIdsToRemove.push_back(track.trackId);
                        }
                        else
                        {
                            // Track is unique, keep it
                            uniqueTrackKeys.insert(uniqueTrackKey);
                        }
                    }
                    spdlog::info("Found {} duplicates in working set {}", trackIdsToRemove.size(), m_workingSetId);
                    if (!trackIdsToRemove.empty())
                    {
                        theTrackLibrary.getWorkingSetManager().removeTracksFromWorkingSet(m_workingSetId, trackIdsToRemove);
                    }
                }

            private:
                const WorkingSetId m_workingSetId;
            };

            auto *task = new RemoveDuplicatesFromWorkingSet{workingSetId};
            TaskDialog::launch("Removing Duplicates",
                task,
                TaskDialog::AutoCloseMode::NoAutoClose, 500, this,
                [this](bool /*success*/)
                {
                    m_dataViewComponent.refreshView();
                });
            task->release(REFCOUNT_DEBUG_ARGS);
        }

        void MainComponent::onCheckFilesForSelectedRows()
        {
            if (!m_currentNode)
            {
                return;
            }

            const auto selectedRows = m_dataViewComponent.getSelectedRowIndices();
            if (selectedRows.empty())
            {
                m_statusPanel.getStatusBar().postMessage("No rows selected to check.", true);
                return;
            }

            auto trackResult = m_currentNode->getTrackInfosForOperation(selectedRows);
            if (trackResult.trackInfos.empty())
            {
                m_statusPanel.getStatusBar().postMessage(trackResult.nonApplicableCount > 0 ? "The selected items are not tracks." : "No tracks selected to check.",
                    true);
                return;
            }

            checkFilesForTracks(std::move(trackResult.trackInfos));
        }

        void MainComponent::checkFilesForTracks(std::vector<TrackInfo> tracks)
        {
            // Paths are resolved here, on the message thread: reconstructFullPath() reads the library's
            // folder cache, which a worker has no business touching. The worker gets plain values and
            // only stats and updates.
            std::vector<background_tasks::MissingFileCheckTask::WorkItem> items;
            items.reserve(tracks.size());
            for (const auto &track : tracks)
            {
                items.push_back({track.trackId, track.reconstructFullPath(), track.filename, track.is_missing});
            }

            const auto trackCount = items.size();

            // Refreshing is driven by the task rather than by the dialog's completion callback, because
            // that callback is discarded if the window is closed with its title-bar button - and tracks
            // are committed one at a time, so an abandoned run still leaves rows corrected in the
            // database and stale on screen.
            juce::Component::SafePointer<MainComponent> safeThis{this};
            auto *task = new background_tasks::MissingFileCheckTask{std::move(items),
                [safeThis]()
                {
                    // Called on the worker thread; the view belongs to the message thread.
                    juce::MessageManager::callAsync(
                        [safeThis]()
                        {
                            if (safeThis != nullptr)
                            {
                                safeThis->m_dataViewComponent.refreshView(true);
                            }
                        });
                }};

            TaskDialog::launch("Checking Files",
                task,
                TaskDialog::AutoCloseMode::Immediate,
                0,
                this,
                [this, task](bool success)
                {
                    // Only the summary message lives here. The refresh above happens either way; losing
                    // this line when the user dismisses the dialog is no loss.
                    if (!success)
                    {
                        return;
                    }

                    const auto recovered = task->getRecoveredCount();
                    const auto newlyMissing = task->getNewlyMissingCount();
                    const auto stillMissing = task->getStillMissingCount();

                    if (recovered == 0 && newlyMissing == 0)
                    {
                        m_statusPanel.getStatusBar().postMessage(
                            stillMissing > 0 ? std::format("Nothing changed; {} track(s) are still missing.", stillMissing) : "All files are where they should be.",
                            stillMissing > 0);
                        return;
                    }

                    std::string message;
                    if (recovered > 0)
                    {
                        message = std::format("{} track(s) found again", recovered);
                    }
                    if (newlyMissing > 0)
                    {
                        message += message.empty() ? "" : ", ";
                        message += std::format("{} track(s) now missing", newlyMissing);
                    }
                    m_statusPanel.getStatusBar().postMessage(message + ".", newlyMissing > 0);
                });

            task->release(REFCOUNT_DEBUG_ARGS);
            spdlog::info("Checking {} track(s) for missing files.", trackCount);
        }

        void MainComponent::onRunBpmAnalysisForSelectedRows()
        {
            // Get selected rows
            const auto selectedRows = m_dataViewComponent.getSelectedRowIndices();
            if (selectedRows.empty())
            {
                m_statusPanel.getStatusBar().postMessage("No rows selected for analysis.", true);
                return;
            }

            // Use the new Node-Centric method to get track IDs
            const auto trackResult = m_currentNode->getTrackInfosForOperation(selectedRows);

            if (trackResult.trackInfos.empty())
            {
                if (trackResult.nonApplicableCount > 0)
                {
                    m_statusPanel.getStatusBar().postMessage(std::format("The selected items are not tracks and cannot be analyzed."), true);
                }
                else
                {
                    m_statusPanel.getStatusBar().postMessage("No tracks selected for analysis.", true);
                }
                return;
            }

            // Show a note if some selections were skipped
            if (trackResult.nonApplicableCount > 0)
            {
                m_statusPanel.getStatusBar().postMessage(
                    std::format("Analyzing {} tracks ({} non-track items skipped)", trackResult.trackInfos.size(), trackResult.nonApplicableCount), false);
            }

            auto *task = new background_tasks::BpmAnalysisTask{std::move(trackResult.trackInfos)};
            TaskDialog::launch("BPM Analysis",
                task,
                TaskDialog::AutoCloseMode::WithDelay,
                500,  // Show success briefly
                this,
                [this, task](bool /*success*/)
                {
                    m_dataViewComponent.refreshView();

                    // Check for bad files
                    const auto &badFiles = task->getBadFiles();
                    if (!badFiles.empty())
                    {
                        showBadFilesDialog(badFiles);
                    }
                });
            task->release(REFCOUNT_DEBUG_ARGS);
        }

        void MainComponent::showBadFilesDialog(const std::vector<database::TrackInfo> &badFiles)
        {
            juce::String message = "The following files could not be analyzed due to unsupported decoder format:\n\n";

            for (const auto &track : badFiles)
            {
                const auto trackPath{track.reconstructFullPath()};
                message += juce::String(trackPath.filename().string()) + juce::String("\n");
            }

            message += "\nWould you like to remove these files from all working sets?\n";
            message += "(They will remain in the library but marked as bad format,\n";
            message += "and won't be included in future BPM analysis or playback)";

            juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon,
                "Bad Files Detected",
                message,
                "Remove from Working Sets",
                "Keep in Working Sets",
                this,
                juce::ModalCallbackFunction::create(
                    [this, badFiles](int result)
                    {
                        if (result == 1) // OK button clicked
                        {
                            // Collect track IDs
                            std::vector<TrackId> badTrackIds;
                            badTrackIds.reserve(badFiles.size());
                            for (const auto &track : badFiles)
                            {
                                badTrackIds.push_back(track.trackId);
                            }

                            // Remove bad files from all working sets
                            auto &wsManager = theTrackLibrary.getWorkingSetManager();
                            const auto allWorkingSets = wsManager.getWorkingSets(database::TrackQueryArgs{});

                            int removedCount = 0;
                            for (const auto &ws : allWorkingSets)
                            {
                                // Try to remove all bad tracks from this working set
                                // The method will ignore tracks that aren't in the set
                                if (wsManager.removeTracksFromWorkingSet(ws.id, badTrackIds))
                                {
                                    removedCount++;
                                }
                            }

                            m_statusPanel.getStatusBar().postMessage(
                                std::format("Marked {} bad files and removed from {} working sets", badFiles.size(), removedCount), false);

                            // Refresh the view to show updated working sets
                            m_dataViewComponent.refreshView();
                            // m_navigationPanel.refreshCurrentNode();
                        }
                    }));
        }

        // --- Action Execution Method Stubs ---
        void MainComponent::playDataRow(RowIndex_t rowIndex)
        {
            if (!m_currentNode)
            {
                m_statusPanel.getStatusBar().postMessage("No node selected for playback.", true);
                return;
            }

            // Use the Node-Centric method with a single row
            const auto trackResult = m_currentNode->getTrackInfosForOperation({rowIndex});
            if (trackResult.trackInfos.empty())
            {
                m_statusPanel.getStatusBar().postMessage("No track info available for row: " + std::to_string(rowIndex), true);
                return;
            }

            const auto &track = trackResult.trackInfos[0];

            spdlog::info("playDataRow: trackId={}, filename={}, folderId={}", track.trackId, track.filename, track.folderId);

            const auto trackPath{track.reconstructFullPath()};

            spdlog::info("playDataRow: reconstructed path = '{}'", trackPath.string());

            juce::File audioFile{jucePathFromFs(trackPath)};

            spdlog::info("playDataRow: juce::File path = '{}', exists = {}", audioFile.getFullPathName().toStdString(), audioFile.existsAsFile());

            if (audioFile.existsAsFile())
            {
                // Stop any mix playback before starting single track
                if (m_playbackController.isPlaying())
                {
                    m_playbackController.stop();
                }

                // uncomment this line, and you get the exceptio
                m_statusPanel.getStatusBar().postMessage(getSafeDisplayText("Playing: " + audioFile.getFileName()), false);
                checkAndUpdateAudioDevice();
                if (!m_playbackController.loadAndPlayFile(audioFile))
                {
                    m_statusPanel.getStatusBar().postMessage(getSafeDisplayText("Error playing: " + audioFile.getFileName()), true);
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, "Playback Error", "Cannot play file:\n" + audioFile.getFullPathName());

                    // Update track status to bad_format if it wasn't already marked
                    if (track.status != database::TrackStatus::BadFormat)
                    {
                        theTrackLibrary.getTrackDatabase().updateTrackStatus(track.trackId, database::TrackStatus::BadFormat);
                    }
                }
                else
                {
                    // Update track status to ok if it wasn't already marked
                    if (track.status != database::TrackStatus::Ok)
                    {
                        theTrackLibrary.getTrackDatabase().updateTrackStatus(track.trackId, database::TrackStatus::Ok);
                    }

                    // Update play count and last_played timestamp
                    theTrackLibrary.getTrackDatabase().incrementTrackPlayCount(track.trackId);

                    // Refresh the data view so play count / last_played are visible immediately
                    if (m_currentNode)
                    {
                        m_currentNode->refreshCache(true);
                        m_dataViewComponent.refreshView();
                    }

                    // Load waveform when playback starts successfully
                    m_enhancedPlayer.loadFile(audioFile, std::format("{} / {} / {}", track.artist_name, track.album_title, track.title), track.trackId);

                    // Load markers for this track
                    const auto markers = theTrackLibrary.getMarkerManager().getMarkersForTrack(track.trackId);
                    m_enhancedPlayer.setMarkers(markers);

                    // Update DataView to highlight the now-playing track
                    m_dataViewComponent.setPlayingTrackId(track.trackId);
                }
            }
            else
            {
                m_statusPanel.getStatusBar().postMessage("Cannot play: " + std::to_string(track.trackId) + " (No path)", true);
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, "Playback Error", "Cannot find audio file for: " + std::to_string(track.trackId));
            }
            // UI will update via timer in EnhancedPlayerComponent
        }

        void MainComponent::playAllFromRow(RowIndex_t startRow)
        {
            if (!m_currentNode)
            {
                m_statusPanel.getStatusBar().postMessage("No node selected for playlist playback.", true);
                return;
            }

            // Get the track at the clicked row (handles folder rows correctly via virtual dispatch)
            const auto clickedTrackResult = m_currentNode->getTrackInfosForOperation({startRow});
            if (clickedTrackResult.trackInfos.empty())
            {
                m_statusPanel.getStatusBar().postMessage("No track at selected row.", true);
                return;
            }
            const auto& clickedTrack = clickedTrackResult.trackInfos[0];

            // Get all tracks from the current view (this returns only tracks, no folder rows)
            const auto allTracks = m_currentNode->getAllTrackInfosForOperation();
            if (allTracks.trackInfos.empty())
            {
                m_statusPanel.getStatusBar().postMessage("No tracks available for playlist.", true);
                return;
            }

            // Find the index of the clicked track in the track array by matching trackId
            size_t startIndex = 0;
            for (size_t i = 0; i < allTracks.trackInfos.size(); ++i)
            {
                if (allTracks.trackInfos[i].trackId == clickedTrack.trackId)
                {
                    startIndex = i;
                    break;
                }
            }

            // Set up the playlist in PlaybackController
            m_playbackController.setPlaylist(
                allTracks.trackInfos,
                startIndex,
                PlaylistSource::Folder,
                m_currentNode->getName()  // Use node name as source context
            );

            spdlog::info("[MainComponent] Started playlist with {} tracks from row {} (track ID {}, playlist index {})",
                        allTracks.trackInfos.size(), startRow, clickedTrack.trackId, startIndex);
        }

        void MainComponent::onDataActionRemoveNamedObjects()
        {
            if (!m_currentNode)
            {
                m_statusPanel.getStatusBar().postMessage("No data node selected.", true);
                return;
            }
            if (m_currentMainView == MainViewType::MixEditor)
            {
                m_statusPanel.getStatusBar().postMessage("Cannot delete rows in Mix Editor view.", true);
                return;
            }

            const auto selectedRows = m_dataViewComponent.getSelectedRowIndices();
            if (selectedRows.empty())
            {
                m_statusPanel.getStatusBar().postMessage("No rows selected for removal.", true);
                return;
            }

            // Use the new analyzeDeletionRequest method to get structured deletion information
            const auto analysisResult = m_currentNode->analyzeDeletionRequest(selectedRows);

            // Check if anything can actually be deleted
            if (analysisResult.deletableObjectIds.empty())
            {
                if (analysisResult.nonDeletableCount > 0)
                {
                    m_statusPanel.getStatusBar().postMessage(
                        std::format("The selected {} cannot be removed.",
                            analysisResult.nonDeletableCount == 1 ? analysisResult.itemTypeSingular : analysisResult.itemTypePlural),
                        true);
                }
                else
                {
                    m_statusPanel.getStatusBar().postMessage("Nothing to remove.", true);
                }
                return;
            }

            // Build the warning message using the analysis result
            std::string warningMessage;
            std::string okButtonText;
            const auto nodeName{m_currentNode->getName()};
            const auto deletableCount = analysisResult.deletableObjectIds.size();

            if (deletableCount == 1 && !analysisResult.singleItemName.empty())
            {
                warningMessage = std::format("Do you want to remove the {} \"{}\" from the {} \"{}\"?",
                    analysisResult.itemTypeSingular,
                    analysisResult.singleItemName,
                    m_currentNode->m_refTypeNameForSingleObject,
                    nodeName);
                okButtonText = std::format("Remove {}", analysisResult.itemTypeSingular);
            }
            else
            {
                warningMessage = std::format("Do you want to remove {} {} from the {} \"{}\"?",
                    deletableCount,
                    analysisResult.itemTypePlural,
                    m_currentNode->m_refTypeNameForSingleObject,
                    nodeName);
                okButtonText = std::format("Remove {} {}", deletableCount, analysisResult.itemTypePlural);
            }

            // Add note about non-deletable items if any
            if (analysisResult.nonDeletableCount > 0)
            {
                warningMessage += std::format("\n\nNote: {} {} cannot be removed and will be skipped.",
                    analysisResult.nonDeletableCount,
                    analysisResult.nonDeletableCount == 1 ? "item" : "items");
            }

            // Capture the node and the list of safe object IDs for the callback
            m_currentNode->retain(REFCOUNT_DEBUG_ARGS); // Ensure node stays alive during async operation

            juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon,
                "Confirm Removal",
                warningMessage,
                okButtonText,
                "Cancel",
                nullptr,
                juce::ModalCallbackFunction::create(
                    [this, node = m_currentNode, analysisResult](int result)
                    {
                        if (result == 1) // OK button clicked
                        {
                            // Stop playback before modifying data to prevent audio thread issues
                            stopMixPlayback();

                            // Use the safe list of object IDs
                            bool removalSuccess = node->removeObjects(analysisResult.deletableObjectIds);

                            if (removalSuccess)
                            {
                                // Refresh the node's cache
                                node->refreshCache(true);

                                // If in mix editor view and this is a mix node, reload the mix editor
                                if (m_currentMainView == MainViewType::MixEditor)
                                {
                                    if (auto *mixNode = dynamic_cast<database::MixNode *>(node))
                                    {
                                        m_mixEditorComponent.loadMix(mixNode);
                                    }
                                }

                                // Refresh the data view. Clear selection too: the removed rows are gone and rows
                                // below them have shifted up, so the old row indices no longer mean anything.
                                m_dataViewComponent.clearSelection();
                                m_dataViewComponent.refreshView();

                                // Show success message
                                const auto itemCount = analysisResult.deletableObjectIds.size();
                                const auto itemType = itemCount == 1 ? analysisResult.itemTypeSingular : analysisResult.itemTypePlural;
                                m_statusPanel.getStatusBar().postMessage(std::format("Removed {} {} from {}", itemCount, itemType, node->getName()), false);
                            }
                            else
                            {
                                m_statusPanel.getStatusBar().postMessage(std::format("Failed to remove items from {}", node->getName()), true);
                            }
                        }

                        // Release the node reference we retained earlier
                        node->release(REFCOUNT_DEBUG_ARGS);
                    }));
        }

        void MainComponent::onDeleteTracksFromLibrary()
        {
            if (!m_currentNode)
            {
                m_statusPanel.getStatusBar().postMessage("No data node selected.", true);
                return;
            }

            const auto selectedRows = m_dataViewComponent.getSelectedRowIndices();
            if (selectedRows.empty())
            {
                m_statusPanel.getStatusBar().postMessage("No tracks selected for deletion.", true);
                return;
            }

            // Get track IDs from selected rows using the public API
            const auto trackResult = m_currentNode->getTrackInfosForOperation(selectedRows);
            if (trackResult.trackInfos.empty())
            {
                if (trackResult.nonApplicableCount > 0)
                {
                    m_statusPanel.getStatusBar().postMessage("The selected items are not tracks and cannot be deleted from the library.", true);
                }
                else
                {
                    m_statusPanel.getStatusBar().postMessage("No valid tracks selected for deletion.", true);
                }
                return;
            }

            // Extract track IDs
            std::vector<TrackId> trackIds;
            trackIds.reserve(trackResult.trackInfos.size());
            for (const auto& trackInfo : trackResult.trackInfos)
            {
                trackIds.push_back(trackInfo.trackId);
            }

            // Build the confirmation message
            const std::string message = std::format("Are you sure you want to delete {} track(s) from the library?", trackIds.size());

            // Create a custom dialog with three buttons
            auto* alertWindow = new juce::AlertWindow("Delete Tracks from Library",
                message,
                juce::AlertWindow::WarningIcon);

            alertWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            alertWindow->addButton("Remove from Library", 1);
            alertWindow->addButton("Remove from Library and Hard Drive", 2);

            // Capture necessary data for the modal callback
            m_currentNode->retain(REFCOUNT_DEBUG_ARGS);

            alertWindow->enterModalState(true,
                juce::ModalCallbackFunction::create(
                    [this, node = m_currentNode, trackIds](int result)
                    {
                        if (result == 1 || result == 2) // Remove from Library OR Remove from Library and Hard Drive
                        {
                            const bool deleteFiles = (result == 2);

                            // Stop playback before modifying data
                            stopMixPlayback();

                            // Call the node's deletion method
                            const bool success = node->deleteTracksFromLibrary(trackIds, deleteFiles);

                            if (success)
                            {
                                // Refresh the data view; clear selection because deleted rows are gone and the
                                // row indices below them have shifted.
                                m_dataViewComponent.clearSelection();
                                m_dataViewComponent.refreshView();

                                // Show success message
                                const std::string fileInfo = deleteFiles ? " and files" : "";
                                m_statusPanel.getStatusBar().postMessage(
                                    std::format("Deleted {} track(s) from library{}", trackIds.size(), fileInfo), false);
                            }
                            else
                            {
                                m_statusPanel.getStatusBar().postMessage("Failed to delete tracks from library", true);
                            }
                        }

                        // Release the node reference
                        node->release(REFCOUNT_DEBUG_ARGS);
                    }),
                true);
        }

        bool MainComponent::createWorkingSet()
        {
            if (!m_currentNode)
            {
                m_statusPanel.getStatusBar().postMessage("No data node selected to create working set from.", true);
                return false;
            }

            if (m_currentMainView == MainViewType::MixEditor)
            {
                m_statusPanel.getStatusBar().postMessage("Cannot create working set in Mix Editor view.", true);
                return false;
            }

            // Check if there are selected rows
            const auto selectedRows = m_dataViewComponent.getSelectedRowIndices();
            if (!selectedRows.empty())
            {
                // Use the new Node-Centric method to get track IDs
                const auto trackResult = m_currentNode->getTrackInfosForOperation(selectedRows);

                if (trackResult.trackInfos.empty())
                {
                    if (trackResult.nonApplicableCount > 0)
                    {
                        m_statusPanel.getStatusBar().postMessage("The selected items are not tracks and cannot be added to a working set.", true);
                    }
                    else
                    {
                        m_statusPanel.getStatusBar().postMessage("No tracks selected for working set.", true);
                    }
                    return false;
                }

                // Show a note if some selections were skipped
                if (trackResult.nonApplicableCount > 0)
                {
                    m_statusPanel.getStatusBar().postMessage(
                        std::format(
                            "Creating working set from {} tracks ({} non-track items skipped)", trackResult.trackInfos.size(), trackResult.nonApplicableCount),
                        false);
                }

                return createWorkingSetFromTrackInfos(trackResult.trackInfos);
            }
            else // if (m_currentNode)
            {
                assert(m_currentNode != nullptr);
                return createWorkingSetFromNode(m_currentNode);
            }
            // PVS-Studio detected this as unreadable code: we're returning early if m_currentNode is null at the entry of the method
            //m_statusPanel.getStatusBar().postMessage("Internal error: no selection, and no current node?", true);
            //return false;
        }

        bool MainComponent::createWorkingSetFromTrackInfos(std::vector<TrackInfo> trackInfos)
        {
            return onHandleCreateWorkingSetDialog(static_cast<int64_t>(trackInfos.size()),
                [this, trackInfos](const juce::String &name, WorkingSetId targetWsId)
                {
                    onCreateWorkingSetFromTrackInfosCallback(name, targetWsId, trackInfos);
                });
        }

        void MainComponent::onCommonCreateWorkingSetCallback(bool success, const WorkingSetInfo &workingSetInfo)
        {
            if (success)
            {
                m_statusPanel.getStatusBar().postMessage("Working set '" + workingSetInfo.name + "' created successfully.", false);
                m_navigationTree.onWorkingSetCreated(workingSetInfo.id);
            }
            else
            {
                m_statusPanel.getStatusBar().postMessage("Failed to create working set: " + workingSetInfo.name, true);
            }
        }

        void MainComponent::onCreateWorkingSetFromTrackInfosCallback(const juce::String &name, WorkingSetId targetWsId, std::vector<TrackInfo> trackInfos)
        {
            WorkingSetInfo workingSetInfo;

            if (targetWsId == -1)
            {
                // Create new working set
                onCommonCreateWorkingSetCallback(
                    theTrackLibrary.getWorkingSetManager().createWorkingSetFromTrackInfos(trackInfos, name.toStdString(), workingSetInfo), workingSetInfo);
            }
            else
            {
                // Append to existing working set
                bool success = theTrackLibrary.getWorkingSetManager().addToWorkingSet(targetWsId, trackInfos);
                if (success)
                {
                    // Get the working set name for the success message
                    auto workingSets = theTrackLibrary.getWorkingSetManager().getWorkingSets({});
                    auto it = std::find_if(workingSets.begin(),
                        workingSets.end(),
                        [targetWsId](const WorkingSetInfo &ws)
                        {
                            return ws.id == targetWsId;
                        });

                    if (it != workingSets.end())
                    {
                        workingSetInfo = *it;
                        m_statusPanel.getStatusBar().postMessage(
                            std::format("Added {} tracks to working set '{}'", trackInfos.size(), workingSetInfo.name), false);
                        // Refresh the working set node to show the new track count
                        m_navigationTree.onWorkingSetCreated(targetWsId);
                    }
                }
                else
                {
                    m_statusPanel.getStatusBar().postMessage("Failed to append tracks to working set", true);
                }
            }
        }

        bool MainComponent::createWorkingSetFromNode(const INavigationNode *node)
        {
            int64_t trackCount;
            if (!node->getTotalTrackCount(trackCount))
            {
                m_statusPanel.getStatusBar().postMessage("Error retrieving track count from node.", true);
                return false;
            }
            node->retain(REFCOUNT_DEBUG_ARGS); // Retain the node to ensure it stays valid during working set creation
            return onHandleCreateWorkingSetDialog(trackCount,
                [this, node](const juce::String &name, WorkingSetId targetWsId)
                {
                    onCreateWorkingSetFromNodeCallback(name, targetWsId, node);
                    node->release(REFCOUNT_DEBUG_ARGS); // Release the node after working set creation
                });
        }

        bool MainComponent::onHandleCreateWorkingSetDialog(int64_t trackCount, OnCreateWorkingSetCallback callback)
        {
            if (trackCount <= 0)
            {
                m_statusPanel.getStatusBar().postMessage("No tracks available for working-set creation", true);
                return false;
            }
            auto *dialog = new CreateWorkingSetDialogComponent{trackCount, callback};

            juce::DialogWindow::LaunchOptions launchOptions;
            launchOptions.escapeKeyTriggersCloseButton = true;
            launchOptions.resizable = false;
            launchOptions.componentToCentreAround = this;

            SingletonComponentDialog::showComponent("CreateWorkingSet", "Create Working Set", dialog, launchOptions,
                true); // modal-style
            return true;
        }

        void MainComponent::onCreateWorkingSetFromNodeCallback(const juce::String &name, WorkingSetId targetWsId, const INavigationNode *node)
        {
            assert(node != nullptr);
            WorkingSetInfo workingSetInfo;

            if (targetWsId == -1)
            {
                // Create new working set
                // Check if this is a VirtualFolderNode
                if (const auto *virtualFolderNode = dynamic_cast<const VirtualFolderNode *>(node))
                {
                    // Use the new recursive method for virtual folders
                    onCommonCreateWorkingSetCallback(theTrackLibrary.getWorkingSetManager().createWorkingSetFromVirtualFolder(
                                                         virtualFolderNode->getFolderId(), name.toStdString(), workingSetInfo, true),
                        workingSetInfo);
                }
                else
                {
                    // Use the standard query-based method for other nodes
                    onCommonCreateWorkingSetCallback(
                        theTrackLibrary.getWorkingSetManager().createWorkingSetFromQuery(*node->getQueryArgs(), name.toStdString(), workingSetInfo),
                        workingSetInfo);
                }
            }
            else
            {
                // Append to existing working set
                bool success = false;
                int64_t addedTrackCount = 0;

                if (const auto *virtualFolderNode = dynamic_cast<const VirtualFolderNode *>(node))
                {
                    success = theTrackLibrary.getWorkingSetManager().addVirtualFolderToWorkingSet(targetWsId, virtualFolderNode->getFolderId(), true);
                    if (success)
                    {
                        TrackQueryArgs args;
                        const auto &folderDb = theTrackLibrary.getFolderDatabase();
                        const auto allChildFolders = folderDb.getAllChildFolders({virtualFolderNode->getFolderId()});
                        args.folderIds = std::vector<FolderId>{allChildFolders.begin(), allChildFolders.end()};
                        args.usePaging = false;
                        addedTrackCount = static_cast<int64_t>(theTrackLibrary.getTracks(args).size());
                    }
                }
                else
                {
                    const auto trackResult = node->getAllTrackInfosForOperation();
                    success = theTrackLibrary.getWorkingSetManager().addToWorkingSet(targetWsId, trackResult.trackInfos);
                    addedTrackCount = static_cast<int64_t>(trackResult.trackInfos.size());
                }

                if (success)
                {
                    // Get the working set name for the success message
                    auto workingSets = theTrackLibrary.getWorkingSetManager().getWorkingSets({});
                    auto it = std::find_if(workingSets.begin(),
                        workingSets.end(),
                        [targetWsId](const WorkingSetInfo &ws)
                        {
                            return ws.id == targetWsId;
                        });

                    if (it != workingSets.end())
                    {
                        workingSetInfo = *it;
                        m_statusPanel.getStatusBar().postMessage(
                            std::format("Added {} tracks to working set '{}'", addedTrackCount, workingSetInfo.name), false);
                        // Refresh the working set node to show the new track count
                        m_navigationTree.onWorkingSetCreated(targetWsId);
                    }
                }
                else
                {
                    m_statusPanel.getStatusBar().postMessage("Failed to append tracks to working set", true);
                }
            }
        }

        // helper method to get all tracks from a node using the Node-Centric architecture
        std::vector<TrackInfo> getAllTracks(const INavigationNode *node)
        {
            if (node)
            {
                // Use the Node-Centric method to get all valid tracks
                const auto trackResult = node->getAllTrackInfosForOperation();
                return trackResult.trackInfos;
            }
            return {};
        }

        void MainComponent::onExportMix(INavigationNode *selectedNode)
        {
            assert(selectedNode != nullptr && "Selected node should not be null in onExportMix()");

            const auto mixNode{static_cast<MixNode *>(selectedNode)};
            const auto mixInfo{mixNode->getMixInfo()};

            // Create and show the export dialog instead of just a file chooser
            auto *dialog = new ExportMixDialog{mixInfo,
                [this, mixInfo](ExportMixDialog::Result result, const audio::ActiveExportSettings &settings)
                {
                    if (result == ExportMixDialog::Result::ExportNow)
                    {
                        this->onExportMixSettingsReceived(mixInfo, settings);
                    }
                    else if (result == ExportMixDialog::Result::ScheduleForLater)
                    {
                        if (theTrackLibrary.getMixManager().scheduleMixForExport(mixInfo.mixId, settings))
                        {
                            spdlog::info("Mix '{}' scheduled for export", mixInfo.name);
                            m_navigationTree.onMixExportStatusChanged();
                        }
                        else
                        {
                            spdlog::error("Failed to schedule mix '{}' for export", mixInfo.name);
                        }
                    }
                }};

            juce::DialogWindow::LaunchOptions launchOptions;
            launchOptions.content.setOwned(dialog);
            launchOptions.dialogTitle = "Export Mix";
            launchOptions.componentToCentreAround = this;
            launchOptions.escapeKeyTriggersCloseButton = true;
            launchOptions.resizable = false;
            launchOptions.launchAsync();
        }

        void MainComponent::onBatchExport()
        {
            auto *dialog = new BatchExportDialog{
                [this](std::vector<IMixManager::ScheduledExport> mixesToExport)
                {
                    if (mixesToExport.empty())
                        return;

                    stopMixPlayback();

                    BatchExportProgressDialog::launch(std::move(mixesToExport), m_audioLibrary.getMixExporter(), this,
                        [this](bool /*success*/)
                        {
                            m_navigationTree.onMixExportStatusChanged();
                        });
                }};

            juce::DialogWindow::LaunchOptions launchOptions;
            launchOptions.content.setOwned(dialog);
            launchOptions.dialogTitle = "Export Scheduled Mixes";
            launchOptions.componentToCentreAround = this;
            launchOptions.escapeKeyTriggersCloseButton = true;
            launchOptions.resizable = false;
            launchOptions.launchAsync();
        }

        class FinalizeAndExportTask : public ILongRunningTask
        {
        public:
            FinalizeAndExportTask(const MixInfo &mixInfo, const audio::IMixExporter &exporter, const audio::ActiveExportSettings &settings)
                : ILongRunningTask{"Finalizing and Exporting Mix", true},
                  m_mixInfo{mixInfo},
                  m_exporter{exporter},
                  m_settings{settings}
            {
            }

            ~FinalizeAndExportTask() override
            {
            }

            void run(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel) override
            {
                // Step 1: Finalize the mix (prune working set, update status)
                progressCb(-1, "Finalizing mix...");
                if (shouldCancel)
                {
                    completionCb(false, "Cancelled before finalization.");
                    return;
                }

                if (!theTrackLibrary.getMixManager().finalizeMix(m_mixInfo.mixId))
                {
                    completionCb(false, "Failed to finalize the mix in the database.");
                    return;
                }

                // Step 2: Export the audio (using existing logic)
                progressCb(-1, "Exporting audio...");
                if (shouldCancel)
                {
                    completionCb(false, "Cancelled before export.");
                    return;
                }
                const auto exportResult = m_exporter.exportMixToFile(m_mixInfo.mixId,
                        m_settings,
                        [&](float progress, const std::string &message)
                        {
                            progressCb(static_cast<int>(progress * 100.0f), message);
                            return !shouldCancel.load();
                        });

                if (exportResult.success)
                {
                    // Step 3: Post-export cleanup if configured
                    if (config::theSettings.mixEditingSettings.clearWorkingSetAfterExport.get())
                    {
                        progressCb(-1, "Cleaning up working set...");

                        // Get the mix info again to check source_ws_id
                        if (m_mixInfo.source_ws_id > 0)
                        {
                            // Get all tracks in this mix
                            const auto mixTracks = theTrackLibrary.getMixManager().getMixTracks(m_mixInfo.mixId);

                            // Remove each track from the working set
                            for (const auto &mixTrack : mixTracks)
                            {
                                if (!theTrackLibrary.getWorkingSetManager().removeTrackFromWorkingSet(m_mixInfo.source_ws_id, mixTrack.trackId))
                                {
                                    spdlog::warn("Failed to remove track {} from working set {} after export", mixTrack.trackId, m_mixInfo.source_ws_id);
                                }
                            }

                            spdlog::info("Removed {} tracks from working set {} after export", mixTracks.size(), m_mixInfo.source_ws_id);

                            // Set the mix's working_set_id to NULL
                            if (!theTrackLibrary.getMixManager().clearMixWorkingSetId(m_mixInfo.mixId))
                            {
                                spdlog::warn("Failed to clear working_set_id for mix {} after export", m_mixInfo.mixId);
                            }
                            else
                            {
                                spdlog::info("Cleared working_set_id for mix {} after export", m_mixInfo.mixId);
                            }
                        }
                    }

                    const auto filename = pathToString(m_settings.outputPath.filename());

                    // Build completion message with warning info if applicable
                    std::string successMsg;
                    if (exportResult.warningCount > 0)
                    {
                        successMsg = std::format("Mix '{}' exported to:\n{}\n\nWarning: {} track(s) had read errors during export. Some audio may be missing or silent. Check the log for details.",
                            m_mixInfo.name, filename, exportResult.warningCount);
                    }
                    else
                    {
                        successMsg = std::format("Mix '{}' successfully exported to:\n{}", m_mixInfo.name, filename);
                    }

                    // Appended rather than replacing the message above, and kept separate from
                    // warningCount: that counts tracks that failed to read while rendering, which means
                    // part of the audio is silent. This means the audio is fine but nothing was written
                    // down about how it was made. Both can be true at once and they need different fixes.
                    if (!exportResult.recoveryWarning.empty())
                    {
                        successMsg += std::format("\n\n{}", exportResult.recoveryWarning);
                    }

                    completionCb(true, successMsg);
                }
                else
                {
                    // Use detailed failure message if available, otherwise generic message
                    std::string failureMsg = exportResult.message.empty()
                        ? "Unable to export mix to file: " + pathToString(m_settings.outputPath)
                        : "Export failed: " + exportResult.message;
                    completionCb(false, failureMsg);
                }
            }

        private:
            MixInfo m_mixInfo;
            const audio::IMixExporter &m_exporter;
            const audio::ActiveExportSettings m_settings;
        };

        void MainComponent::onExportMixSettingsReceived(const MixInfo &mixInfo, const audio::ActiveExportSettings &settings)
        {
            // Stop any ongoing playback before starting export
            stopMixPlayback();

            spdlog::info("Finalizing and exporting mix ID: {} (Name: '{}') to: {}", mixInfo.mixId, mixInfo.name, pathToString(settings.outputPath));

            auto *task = new FinalizeAndExportTask{mixInfo, m_audioLibrary.getMixExporter(), settings};
            TaskDialog::launch("Finalize & Export", task, TaskDialog::AutoCloseMode::NoAutoClose, 500, this,
                [this](bool /*success*/)
                {
                    // Refresh navigation tree after export completes
                    m_navigationTree.onMixExportStatusChanged();
                });
            task->release(REFCOUNT_DEBUG_ARGS);
            // Note: tags will be deleted by FinalizeAndExportTask destructor
        }

        void MainComponent::onEditWorkingSetMetadata(INavigationNode *node)
        {
            if (auto *wsNode = dynamic_cast<WorkingSetNode *>(node))
            {
                const auto wsInfo = wsNode->getWorkingSetInfo();

                node->retain(REFCOUNT_DEBUG_ARGS); // Retain the node to ensure it stays valid during metadata editing
                auto *dialog = new EditWorkingSetMetaDataDialog{wsInfo,
                    [this, node](bool nameChanged, std::string_view newName)
                    {
                        if (nameChanged)
                        {
                            m_navigationTree.onNodeRenamed(node, newName);
                        }
                        node->release(REFCOUNT_DEBUG_ARGS); // Release the node after editing
                    }};

                juce::DialogWindow::LaunchOptions launchOptions;
                launchOptions.content.setOwned(dialog);
                launchOptions.dialogTitle = "Working Set Details";
                launchOptions.componentToCentreAround = this;
                launchOptions.escapeKeyTriggersCloseButton = true;
                launchOptions.resizable = false;
                launchOptions.launchAsync();
            }
            else
            {
                // Handle other node types here if needed in the future
                m_statusPanel.getStatusBar().postMessage("Details not available for this item.", true);
            }
        }

        void MainComponent::onEditMixMetadata(INavigationNode *node)
        {
            if (auto *mixNode = dynamic_cast<MixNode *>(node))
            {
                auto mixInfo = mixNode->getMixInfo();

                // Verify the cached metadata against the database and refresh if needed
                auto& mixManager = database::theTrackLibrary.getMixManager();
                const int actualTrackCount = mixManager.getTrackCountForMix(mixInfo.mixId);

                if (actualTrackCount != mixInfo.numberOfTracks)
                {
                    spdlog::warn("[MainComponent] Metadata mismatch detected for mix {}: cached={} tracks, actual={} tracks. Refreshing metadata...",
                                mixInfo.mixId, mixInfo.numberOfTracks, actualTrackCount);

                    // Force reload the mix to recalculate the correct duration
                    // This ensures the MixProjectLoader has the correct data
                    mixNode->refreshCache(true); // true = flush cache and reload

                    // The reload replaced the loader's track vector, and the timeline holds pointers
                    // into it. Without this the editor goes on painting and dragging TrackViews that
                    // point at freed memory.
                    m_mixEditorComponent.onNodeCacheReloaded(mixNode);

                    // Get the refreshed metadata (which now has the correct track count and duration)
                    mixInfo = mixNode->getMixInfo();

                    spdlog::info("[MainComponent] Metadata refreshed: {} tracks, {} duration",
                                mixInfo.numberOfTracks, mixInfo.totalDuration.count());
                }

                node->retain(REFCOUNT_DEBUG_ARGS); // Retain the node to ensure it stays valid during metadata editing
                auto *dialog = new EditMixMetaDataDialog{mixInfo,
                    [this, node](bool nameChanged, std::string_view newName)
                    {
                        if (nameChanged)
                        {
                            m_navigationTree.onNodeRenamed(node, newName);
                        }
                        node->release(REFCOUNT_DEBUG_ARGS); // Release the node after editing
                    }};

                juce::DialogWindow::LaunchOptions launchOptions;
                launchOptions.content.setOwned(dialog);
                launchOptions.dialogTitle = "Mix Details";
                launchOptions.componentToCentreAround = this;
                launchOptions.escapeKeyTriggersCloseButton = true;
                launchOptions.resizable = false;
                launchOptions.launchAsync();
            }
            else
            {
                // Handle other node types here if needed in the future
                m_statusPanel.getStatusBar().postMessage("Details not available for this item.", true);
            }
        }
        
        void MainComponent::onUnlockMixForEditing(INavigationNode *node)
        {
            if (auto *mixNode = dynamic_cast<MixNode *>(node))
            {
                const auto mixInfo = mixNode->getMixInfo();

                // Show dialog with three options
                const auto options = juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::QuestionIcon)
                    .withTitle("Edit Exported Mix")
                    .withMessage("This mix has been exported. How would you like to edit it?")
                    .withButton("Move Back to Mixes")
                    .withButton("Clone as New Mix")
                    .withButton("Cancel");

                juce::AlertWindow::showAsync(options, [mixNode, mixInfo, this](int result)
                {
                    if (result == 1) // Move Back to Mixes
                    {
                        const auto& mixManager = database::theTrackLibrary.getMixManager();
                        if (mixManager.moveBackToMixes(mixInfo.mixId))
                        {
                            spdlog::info("Unlocked mix {} for editing", mixInfo.mixId);
                            m_statusPanel.getStatusBar().postMessage("Mix moved back to Mixes for editing", false);

                            // Refresh the MixNode to update its cached MixInfo
                            mixNode->refreshMixInfo();

                            // Refresh navigation tree to show the change immediately
                            m_navigationTree.onMixExportStatusChanged();

                            // If the mix is currently loaded in the editor, reload it to update read-only state
                            if (m_mixEditorComponent.getCurrentMixNode() &&
                                m_mixEditorComponent.getCurrentMixNode()->getMixInfo().mixId == mixInfo.mixId)
                            {
                                m_mixEditorComponent.forceRefresh();
                            }
                        }
                        else
                        {
                            spdlog::error("Failed to unlock mix {} for editing", mixInfo.mixId);
                            m_statusPanel.getStatusBar().postMessage("Failed to unlock mix", true);
                        }
                    }
                    else if (result == 2) // Clone as New Mix
                    {
                        this->handleCloneMix(mixInfo);
                    }
                });
            }
        }

        juce::String MainComponent::generateCloneName(const std::string& originalName)
        {
            // Generate a clone name similar to CreateMixDialogComponent style
            auto now = std::time(nullptr);
#ifdef _MSC_VER // Use localtime_s on Windows
            std::tm tm_s;
            localtime_s(&tm_s, &now);
            auto tm = tm_s;
#else // Use localtime on other platforms
            auto tm = *std::localtime(&now);
#endif

            std::ostringstream oss;
            oss << originalName << " (Copy " << std::put_time(&tm, "%Y-%m-%d %H-%M-%S") << ")";
            return juce::String(oss.str());
        }

        void MainComponent::handleCloneMix(const database::MixInfo& mixInfo)
        {
            // Get all tracks from the original mix
            const auto& mixManager = database::theTrackLibrary.getMixManager();
            const auto mixTracks = mixManager.getMixTracks(mixInfo.mixId);

            if (mixTracks.empty())
            {
                m_statusPanel.getStatusBar().postMessage("Cannot clone empty mix", true);
                return;
            }

            // Show dialog to let user customize the clone name
            auto defaultCloneName = generateCloneName(mixInfo.name);

            // Create the alert window for clone name input
            auto* alertWindow = new juce::AlertWindow("Clone Mix",
                                                      "Enter name for the cloned mix:",
                                                      juce::AlertWindow::NoIcon);
            alertWindow->addTextEditor("cloneName", defaultCloneName, "Mix Name:");
            alertWindow->addButton("Create Clone", 1, juce::KeyPress(juce::KeyPress::returnKey));
            alertWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            focusTextEditorOnOpen(*alertWindow, "cloneName");

            // Select all text in the editor for easy replacement
            if (auto* editor = alertWindow->getTextEditor("cloneName"))
            {
                editor->selectAll();
            }

            alertWindow->enterModalState(true,
                juce::ModalCallbackFunction::create(
                    [this, mixInfo, mixTracks, alertWindow](int result)
                    {
                        std::unique_ptr<juce::AlertWindow> windowDeleter{alertWindow}; // Auto-delete on scope exit //-V824

                        if (result == 1) // Create Clone was clicked
                        {
                            const auto cloneName = alertWindow->getTextEditorContents("cloneName").trim();

                            if (cloneName.isEmpty())
                            {
                                m_statusPanel.getStatusBar().postMessage("Clone name cannot be empty", true);
                            }
                            else
                            {
                                // Create new MixInfo for the clone
                                database::MixInfo cloneMixInfo;
                                assert(cloneMixInfo.mixId == 0); // New mix (use 0, not -1)
                                cloneMixInfo.name = cloneName.toStdString();
                                cloneMixInfo.status = "New"; // Clone starts as editable
                                assert(cloneMixInfo.source_ws_id == 0); // No source working set
                                cloneMixInfo.exportFolder = {}; // Not exported
                                cloneMixInfo.totalDuration = mixInfo.totalDuration; // Copy duration from original
                                cloneMixInfo.numberOfTracks = mixInfo.numberOfTracks; // Copy track count

                                // Create a mutable copy of the tracks with cleared IDs
                                std::vector<database::MixTrack> cloneTracks;
                                cloneTracks.reserve(mixTracks.size());

                                for (const auto& originalTrack : mixTracks)
                                {
                                    database::MixTrack clonedTrack;
                                    assert(clonedTrack.mixId == 0); // Will be set by createOrUpdateMix (use 0, not -1)
                                    clonedTrack.trackId = originalTrack.trackId; // Keep the actual track reference
                                    clonedTrack.orderInMix = originalTrack.orderInMix;
                                    clonedTrack.cueStart = originalTrack.cueStart;
                                    clonedTrack.cueEnd = originalTrack.cueEnd;
                                    clonedTrack.attachFrom = originalTrack.attachFrom;
                                    clonedTrack.attachTo = originalTrack.attachTo;
                                    clonedTrack.envelopePoints = originalTrack.envelopePoints;
                                    clonedTrack.gainAdjustment = originalTrack.gainAdjustment;
                                    cloneTracks.push_back(clonedTrack);
                                }

                                const auto& mixManager = database::theTrackLibrary.getMixManager();
                                if (mixManager.createOrUpdateMix(cloneMixInfo, cloneTracks))
                                {
                                    spdlog::info("Successfully cloned mix {} as new mix '{}' (ID: {})",
                                                 mixInfo.mixId, cloneMixInfo.name, cloneMixInfo.mixId);
                                    m_statusPanel.getStatusBar().postMessage("Mix cloned successfully", false);

                                    // Refresh navigation tree to show the new mix
                                    m_navigationTree.onMixCreated(cloneMixInfo.mixId);
                                }
                                else
                                {
                                    spdlog::error("Failed to clone mix {} with name '{}'",
                                                 mixInfo.mixId, cloneMixInfo.name);
                                    m_statusPanel.getStatusBar().postMessage("Failed to clone mix", true);
                                }
                            }
                        }
                    }),
                true);
        }

        bool MainComponent::navigateToFolder(FolderId folderId)
        {
            auto rootNode{m_navigationTree.getRootNode()};
            auto foldersRootNode = rootNode->getFoldersRootNode();
            rootNode->release(REFCOUNT_DEBUG_ARGS); // Release the root node reference, we don't own it
            if (!foldersRootNode)
            {
                spdlog::error("No folders found in navigation tree root node.");
                return false;
            }
            const EnsureNodeIsReleased enirFolderNodesRoot{foldersRootNode};

            auto &folderDb{theTrackLibrary.getFolderDatabase()};
            const auto parents{folderDb.getParentSet(folderId)};

            const auto underlyingFolder{folderDb.getFolderById(folderId)};
            if (!underlyingFolder)
            {
                spdlog::error("Folder ID {} not found in database.", folderId);
                m_statusPanel.getStatusBar().postMessage("Folder not found in database.", true);
                return false;
            }

            spdlog::info("Navigating to folder ID {}: {}", folderId, underlyingFolder.value().path);

            // Build the path from root to target folder
            std::vector<INavigationNode *> pathToTarget;
            // Don't add foldersRootNode to pathToTarget since we don't own it
            // It's owned by the navigation tree

            auto currentParent = foldersRootNode;
            bool foundTarget = false;

            // Build the complete path
            while (currentParent && currentParent->canExpand())
            {
                std::vector<INavigationNode *> children;
                if (!currentParent->expand(children))
                {
                    spdlog::error("Failed to expand node: {}", currentParent->getName());
                    break;
                }

                INavigationNode *nextInPath = nullptr;
                for (auto childNode : children)
                {
                    const auto childFolderId{childNode->getUniqueId()};
                    if (childFolderId == folderId)
                    {
                        // Found the target folder!
                        pathToTarget.push_back(childNode);
                        // Note: childNode already has refcount 1 from expand(), so we keep it
                        foundTarget = true;
                        spdlog::info("Found target folder: {} (ID: {})", childNode->getName(), childFolderId);
                    }
                    else if (parents.contains(childFolderId))
                    {
                        // This is on the path to our target
                        if (!nextInPath) // Only keep the first matching parent
                        {
                            nextInPath = childNode;
                            // Note: childNode already has refcount 1 from expand(), so we keep it
                            spdlog::info("node {} ({}) is a valid parent", childNode->getName(), childFolderId);
                        }
                        else
                        {
                            // Release any additional matching parents (shouldn't happen but be safe)
                            childNode->release(REFCOUNT_DEBUG_ARGS);
                        }
                    }
                    else
                    {
                        // Not needed, release it
                        childNode->release(REFCOUNT_DEBUG_ARGS);
                    }
                }

                if (foundTarget)
                {
                    // We found the target, but need to release any remaining children we haven't processed yet
                    // Actually, the for loop above processes all children, so we're good
                    break;
                }

                if (nextInPath)
                {
                    pathToTarget.push_back(nextInPath);
                    currentParent = nextInPath;
                }
                else
                {
                    spdlog::warn("Could not find next node in path to target folder");
                    break;
                }
            }

            // Now expand the UI tree along the path
            if (foundTarget && !pathToTarget.empty())
            {
                // pathToTarget now contains only the actual folder nodes (no foldersRootNode)
                // so we can use it directly

                // Use the new method to expand and navigate
                if (m_navigationPanel.expandPathAndSelectTarget(pathToTarget))
                {
                    m_statusPanel.getStatusBar().postMessage("Navigated to folder", false);
                }
                else
                {
                    m_statusPanel.getStatusBar().postMessage("Failed to navigate to folder", true);
                    spdlog::error("Failed to navigate to folder in tree");
                }
            }

            // Clean up - release all retained nodes
            for (auto node : pathToTarget)
            {
                node->release(REFCOUNT_DEBUG_ARGS);
            }

            return foundTarget;
        }

        void MainComponent::onShowInFolder(RowIndex_t rowIndex)
        {
            spdlog::info("onShowInFolder called for row {}", rowIndex);

            // Check if we're viewing albums
            if (const auto *albumsNode = dynamic_cast<database::AlbumsNode *>(m_currentNode))
            {
                // Get the folder ID for the selected album
                const auto folderId = albumsNode->getFolderIdForRow(rowIndex);
                spdlog::info("Album folder ID: {}", folderId);

                if (folderId < 0)
                {
                    m_statusPanel.getStatusBar().postMessage("Cannot navigate to folder", true);
                    return;
                }
                navigateToFolder(folderId);
            }
            else
            {
                m_statusPanel.getStatusBar().postMessage("Show in folder is only available for albums", true);
            }
        }

        void MainComponent::onCopyToClipboard(RowIndex_t rowIndex)
        {
            if (!m_currentNode)
            {
                return;
            }

            auto selectedRows = m_dataViewComponent.getSelectedRowIndices();
            if (selectedRows.empty())
            {
                selectedRows = {rowIndex};
            }

            // Folder-backed rows (albums): copy the underlying filesystem folder path(s).
            if (const auto *albumsNode = dynamic_cast<const database::AlbumsNode *>(m_currentNode))
            {
                juce::StringArray paths;
                for (const auto row : selectedRows)
                {
                    const auto folderId = albumsNode->getFolderIdForRow(row);
                    if (folderId < 0)
                        continue;
                    if (const auto folder = theTrackLibrary.getFolderDatabase().getFolderById(folderId))
                    {
                        const auto &path = folder->actualPath.empty() ? folder->path : folder->actualPath;
                        if (!path.empty())
                            paths.add(juce::String(path));
                    }
                }
                if (paths.isEmpty())
                {
                    m_statusPanel.getStatusBar().postMessage("No folder path available to copy.", true);
                    return;
                }
                copyTextToClipboard(paths.joinIntoString("\n"));
                m_statusPanel.getStatusBar().postMessage(std::format("Copied {} folder path(s) to clipboard.", paths.size()), false);
                return;
            }

            // Track rows: copy a tab-separated table of exactly the columns currently shown
            // (pastes cleanly into a text editor or spreadsheet). Show/hide the Path column to
            // control whether the full path is included.
            const auto table = m_dataViewComponent.getRowsAsTable(selectedRows);
            if (table.isEmpty())
            {
                m_statusPanel.getStatusBar().postMessage("Nothing to copy.", true);
                return;
            }
            copyTextToClipboard(table);
            m_statusPanel.getStatusBar().postMessage(std::format("Copied {} row(s) to clipboard.", selectedRows.size()), false);
        }

        bool MainComponent::showTrackInLibrary(TrackId trackId)
        {
            if (trackId < 0)
            {
                return false;
            }

            auto rootNode{m_navigationTree.getRootNode()};
            if (!rootNode)
            {
                m_statusPanel.getStatusBar().postMessage("Library navigation is unavailable.", true);
                return false;
            }

            auto* libraryNode = rootNode->get(getLibraryRootNodeName());
            rootNode->release(REFCOUNT_DEBUG_ARGS);

            if (!libraryNode)
            {
                m_statusPanel.getStatusBar().postMessage("Library node not found.", true);
                return false;
            }

            navigateToNode(libraryNode);

            if (m_dataViewComponent.selectTrackById(trackId))
            {
                m_statusPanel.getStatusBar().postMessage("Track selected in library.", false);
                return true;
            }

            if (!m_dynamicToolbar.getFilterText().isEmpty())
            {
                m_dynamicToolbar.setFilterText({}, juce::sendNotification);
                if (m_dataViewComponent.selectTrackById(trackId))
                {
                    m_statusPanel.getStatusBar().postMessage("Track selected in library.", false);
                    return true;
                }
            }

            m_statusPanel.getStatusBar().postMessage("Track could not be located in the current library view.", true);
            return false;
        }

        void MainComponent::showTrackDetailsDialog(TrackId trackId)
        {
            const auto trackInfo = theTrackLibrary.getTrackById(trackId);
            if (!trackInfo.has_value())
            {
                m_statusPanel.getStatusBar().postMessage("Track details are unavailable.", true);
                return;
            }
            launchTrackDetailsDialog({*trackInfo});
        }

        void MainComponent::launchTrackDetailsDialog(std::vector<database::TrackInfo> tracks)
        {
            if (tracks.empty())
            {
                m_statusPanel.getStatusBar().postMessage("Track details are unavailable.", true);
                return;
            }

            // Cap how many tracks the (paged) dialog will show; bulk data is what
            // Copy to Clipboard is for. The cap is configurable.
            const auto totalSelected = tracks.size();
            const int maxPages = std::max(1, config::theSettings.uiSettings.maxTrackDetailsPages.get());
            if (totalSelected > static_cast<size_t>(maxPages))
                tracks.resize(static_cast<size_t>(maxPages));

            std::vector<juce::String> pages;
            pages.reserve(tracks.size());
            for (const auto &t : tracks)
                pages.push_back(formatTrackDetails(t));

            auto *dialog = new TrackDetailsDialog{std::move(pages)};
            juce::DialogWindow::LaunchOptions launchOptions;
            launchOptions.content.setOwned(dialog);
            launchOptions.dialogTitle = "Track Details";
            launchOptions.componentToCentreAround = this;
            launchOptions.escapeKeyTriggersCloseButton = true;
            launchOptions.resizable = true;
            launchOptions.launchAsync();

            if (totalSelected > static_cast<size_t>(maxPages))
            {
                m_statusPanel.getStatusBar().postMessage(
                    std::format("Showing details for the first {} of {} selected tracks.", maxPages, totalSelected), false);
            }
        }

        void MainComponent::onDataActionDelete(INavigationNode *node)
        {
            assert(node != nullptr && "Selected node should not be null in onDataActionDelete()");

            const auto name{node->getName()};
            const auto message{std::format("Are you sure you want to delete the {} {}?", node->m_refTypeNameForSingleObject, name)};
            const auto caption{std::format("Delete {}", node->m_refTypeNameForSingleObject)};

            node->retain(REFCOUNT_DEBUG_ARGS); // Retain the node to ensure it stays valid during deletion
            juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon,
                "Question",
                message,
                caption,
                "Cancel",
                nullptr,                             // Parent component (optional, nullptr for desktop)
                juce::ModalCallbackFunction::create( // Callback
                    [this, node](int result)         // Capture necessary data
                    {
                        onDataActionDeleteConfirmed(node, result);
                        node->release(REFCOUNT_DEBUG_ARGS); // Release the node after deletion
                    }));
        }

        void MainComponent::onDataActionDeleteConfirmed(INavigationNode *node, int result)
        {
            if (result == 1)
            {
                const std::string typeName{node->m_refTypeNameForSingleObject};
                const std::string nodeName{node->getName()};
                if (m_navigationTree.deleteObject(node))
                {
                    m_statusPanel.getStatusBar().postMessage(std::format("{} {} successfully removed.", typeName, nodeName));
                    updateTrackCountStatus();
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                        "Deletion Failed",
                        std::format("Could not remove the {} {} from the database.", node->m_refTypeNameForSingleObject, node->getName()));
                }
            }
            else // User clicked "Cancel" (result == 0) or closed the dialog
            {
                m_statusPanel.getStatusBar().postMessage(std::format("{} deletion cancelled.", node->m_refTypeNameForSingleObject), false);
            }
        }

        void MainComponent::createMix()
        {
            if (!m_currentNode)
            {
                m_statusPanel.getStatusBar().postMessage("No data node selected.", true);
                return;
            }
            if (m_currentMainView == MainViewType::MixEditor)
            {
                m_statusPanel.getStatusBar().postMessage("Cannot create mix in Mix Editor view.", true);
                return;
            }

            // Capture the source working set ID from the current node (only if it's a WorkingSet)
            const WorkingSetId source_ws_id = m_currentNode->getWorkingSetId();

            // Get selected rows to check what's selected
            const auto selectedRows = m_dataViewComponent.getSelectedRowIndices();
            std::vector<TrackInfo> selectedTracks;

            if (!selectedRows.empty())
            {
                // Use the new Node-Centric method to get full track information
                const auto trackResult = m_currentNode->getTrackInfosForOperation(selectedRows);

                if (trackResult.trackInfos.empty())
                {
                    // Only non-track items selected or no valid tracks
                    if (trackResult.nonApplicableCount > 0)
                    {
                        m_statusPanel.getStatusBar().postMessage("The selected items are not tracks and cannot be added to a mix.", true);
                    }
                    else
                    {
                        m_statusPanel.getStatusBar().postMessage("No valid tracks selected.", true);
                    }
                    return;
                }

                // Use the validated track information
                selectedTracks = trackResult.trackInfos;

                // Show a note if some selections were skipped
                if (trackResult.nonApplicableCount > 0)
                {
                    m_statusPanel.getStatusBar().postMessage(
                        std::format("Creating mix from {} tracks ({} non-track items skipped)", selectedTracks.size(), trackResult.nonApplicableCount), false);
                }
            }

            // If we have 0 or 1 track selected, use all tracks from the node
            if (selectedTracks.size() <= 1)
            {
                selectedTracks = getAllTracks(m_currentNode);
            }

            if (selectedTracks.empty())
            {
                m_statusPanel.getStatusBar().postMessage("Not enough tracks selected to create a mix.", true);
                return;
            }
            auto *dialog = new ui::CreateMixDialogComponent{selectedTracks,
                source_ws_id,
                [this](bool success, const MixInfo &mixInfo)
                {
                    onMixCreatedCallback(success, mixInfo);
                }};

            juce::DialogWindow::LaunchOptions launchOptions;
            launchOptions.escapeKeyTriggersCloseButton = true;
            launchOptions.resizable = false;
            launchOptions.componentToCentreAround = this;

            SingletonComponentDialog::showComponent("CreateMix", "Create Auto-Mix", dialog, launchOptions,
                true); // modal-style
        }

        void MainComponent::onMixCreatedCallback(bool success, const MixInfo &mixInfo)
        {
            if (success)
            {
                m_statusPanel.getStatusBar().postMessage("Mix '" + mixInfo.name + "' created or updated successfully.", false);

                // Check if this mix was already selected BEFORE we call onMixCreated
                bool wasAlreadySelected = false;
                if (m_currentNode)
                {
                    if (auto *mixNode = dynamic_cast<MixNode *>(m_currentNode))
                    {
                        wasAlreadySelected = (mixNode->getUniqueId() == mixInfo.mixId);
                        spdlog::info("onMixCreatedCallback: current node is mix {}, target mix is {}, wasAlreadySelected={}",
                            mixNode->getUniqueId(), mixInfo.mixId, wasAlreadySelected);
                    }
                }

                // This will select the node if it's new, or just refresh the tree if it exists
                m_navigationTree.onMixCreated(mixInfo.mixId);

                // Only force a refresh if the mix was ALREADY selected (meaning it's an update, not a new creation)
                // If it wasn't selected before, onMixCreated->selectNode will handle loading it
                if (wasAlreadySelected)
                {
                    spdlog::info("Mix was already selected, forcing refresh...");
                    if (auto *mixNode = dynamic_cast<MixNode *>(m_currentNode))
                    {
                        spdlog::info("Currently selected mix (ID: {}) was updated, forcing view refresh.", mixInfo.mixId);

                        // Invalidate the node's internal cache to pick up the new tracks
                        mixNode->refreshCache(true);

                        if (m_currentMainView == MainViewType::MixEditor)
                        {
                            spdlog::info("Reloading mix editor...");
                            m_mixEditorComponent.loadMix(mixNode);
                        }
                        else // DataView showing the mix's tracks
                        {
                            spdlog::info("Refreshing data view...");
                            m_dataViewComponent.refreshView();
                        }
                    }
                }
                else
                {
                    spdlog::info("Mix was NOT already selected, onMixCreated will handle selection");
                }
            }
            else
            {
                m_statusPanel.getStatusBar().postMessage("Failed to create mix: " + mixInfo.name, true);
            }
        }

        void MainComponent::requestPlayOrPlaySelection()
        {
            // Always try to play the currently selected track
            if (m_currentMainView == MainViewType::DataView && m_dataViewComponent.getNumSelectedRows() > 0)
            {
                // Get the first selected row and play it
                auto selectedRows = m_dataViewComponent.getSelectedRowIndices();
                if (!selectedRows.empty())
                {
                    playDataRow(selectedRows[0]); // This loads and plays the new track
                    return;
                }
            }
            if (m_currentMainView == MainViewType::MixEditor)
            {
                // In Mix Editor, play from current playhead position
                auto *timeline = &m_mixEditorComponent.getTimeline(); // You'll need to add this getter
                timeline->playFromPosition(timeline->getCurrentTimePosition());
                return;
            }

            // Fallback: if nothing selected, resume current playback if paused
            if (m_playbackController.getState() == PlaybackController::PlayerState::TrackPaused ||
                m_playbackController.getState() == PlaybackController::PlayerState::MixPaused)
            {
                checkAndUpdateAudioDevice();
                m_playbackController.play();
                return;
            }

            // No selection and nothing to resume
            m_statusPanel.getStatusBar().postMessage("No track selected to play.", true);
        }

        bool MainComponent::onShowScanDialog()
        {
            auto *scanDialog = new LibraryRootsComponent{};

            scanDialog->onScanCompleted = [this]()
            {
                spdlog::info("MainComponent: Scan completed. Refreshing views.");

                if (auto rootNavigationNode = m_navigationTree.getRootNode())
                {
                    std::vector<INavigationNode *> children;
                    rootNavigationNode->expand(children);
                    for (const auto node : children)
                    {
                        node->refreshCache(true); // true = flush cache
                        if (auto *treeItem = m_navigationPanel.findTreeViewItemForNode(node))
                        {
                            treeItem->treeHasChanged();
                        }
                        node->release(REFCOUNT_DEBUG_ARGS);
                    }
                    rootNavigationNode->release(REFCOUNT_DEBUG_ARGS); // Release the root node after use
                }

                // Also refresh the data view of the currently selected node. The loop above flushes the
                // root's immediate children, which covers the Library node but not a nested working set
                // or virtual folder the user happens to be sitting in - and a scan is exactly when
                // is_missing changes underneath them.
                if (m_currentMainView == MainViewType::DataView)
                {
                    m_dataViewComponent.refreshView(true);
                }
            };

            juce::DialogWindow::LaunchOptions launchOptions;
            launchOptions.content.setOwned(scanDialog);
            launchOptions.dialogTitle = "Manage Library Folders & Scan";
            launchOptions.escapeKeyTriggersCloseButton = true;
            launchOptions.resizable = true;
            scanDialog->onDialogClosed = [this]()
            {
                spdlog::info("MainComponent: ScanDialogComponent closed");
                if (auto rootNavigationNode = m_navigationTree.getRootNode())
                {
                    std::vector<INavigationNode *> children;
                    rootNavigationNode->expand(children);
                    for (const auto node : children)
                    {
                        node->refreshCache(true); // true = flush cache
                        if (auto *treeItem = m_navigationPanel.findTreeViewItemForNode(node))
                        {
                            treeItem->treeHasChanged();
                        }
                        node->release(REFCOUNT_DEBUG_ARGS);
                    }
                    rootNavigationNode->release(REFCOUNT_DEBUG_ARGS); // Release the root node after use
                }
                // If we are in DataView, we refresh it. Flushing for the same reason as on scan
                // completion: a scan may have run while the dialog was open.
                if (m_currentMainView == MainViewType::DataView)
                {
                    m_dataViewComponent.refreshView(true);
                }

                m_statusPanel.getStatusBar().postMessage("Scan dialog closed.", false);
            };

            launchOptions.launchAsync();
            m_statusPanel.getStatusBar().postMessage("Folder management dialog opened.", false);
            return true;
        }

        bool MainComponent::onShowAboutDialog()
        {
            auto *dialog = new AboutDialog{};

            juce::DialogWindow::LaunchOptions launchOptions;
            launchOptions.content.setOwned(dialog);
            launchOptions.dialogTitle = "About JucyAudio";
            launchOptions.componentToCentreAround = this;
            launchOptions.escapeKeyTriggersCloseButton = true;
            launchOptions.resizable = false;
            launchOptions.launchAsync();

            return true;
        }

        bool MainComponent::onShowConfigureColumnsDialog()
        {
            using namespace config;

            if (m_currentMainView == MainViewType::MixEditor)
            {
                m_statusPanel.getStatusBar().postMessage("Column configuration not available in Mix Editor view.", true);
                return false;
            }
            TypedValueVector<DataViewColumnSection> *pConfigSection = nullptr;
            const auto currentNode = m_dataViewComponent.getCurrentNode();
            if (!currentNode)
            {
                m_statusPanel.getStatusBar().postMessage("No node selected at all.", true);
                return false;
            }
            const auto currentNodeName = currentNode->getName();
            if (currentNodeName.starts_with(getWorkingSetsRootNodeName()))
            {
                pConfigSection = &config::theSettings.uiSettings.workingSetsViewColumns;
            }
            else if (currentNodeName.starts_with(getFoldersRootNodeName()))
            {
                pConfigSection = &config::theSettings.uiSettings.foldersViewColumns;
            }
            else if (currentNodeName.starts_with(getMixesRootNodeName()))
            {
                pConfigSection = &config::theSettings.uiSettings.mixesViewColumns;
            }
            else if (currentNodeName.starts_with(getLibraryRootNodeName()))
            {
                pConfigSection = &config::theSettings.uiSettings.libraryViewColumns;
            }
            else
            {
                m_statusPanel.getStatusBar().postMessage("No valid node selected for column configuration.", true);
                return false;
            }

            const auto &availableCols = currentNode->getColumns();

            auto* columnDialog = new ColumnConfigurationDialogComponent{
    currentNodeName,
    availableCols,
    *pConfigSection,
    [this, currentNode](bool changesMade) { // Callback after ColumnConfigurationDialog closes via OK/Cancel
        if (changesMade) {
            spdlog::info("Column configuration changed. Refreshing DataView.");
            config::TomlBackend backend{g_strConfigFilename};
            theSettings.save(backend); // Save changes to config
            m_dataViewComponent.setCurrentNode(currentNode, true); // Refresh the current node
        }
    }
            };
            // Launch columnDialog modally
            juce::DialogWindow::LaunchOptions lo;
            lo.content.setOwned(columnDialog);
            lo.dialogTitle = "Configure Columns";
            lo.componentToCentreAround = this;
            lo.escapeKeyTriggersCloseButton = true;
            lo.resizable = true; // Maybe allow resize for this one
            lo.launchAsync();
            return true;
        }

        bool MainComponent::isActionAvailable(DataAction action) const
        {
            if (!m_currentNode)
                return false;

            const auto availableActions = m_currentNode->getNodeActions();
            return std::find(availableActions.begin(), availableActions.end(), action) != availableActions.end();
        }

        std::string MainComponent::applyThemeLive(size_t themeIndex)
        {
            // Apply a theme to the whole UI WITHOUT persisting it — used for live preview as well
            // as the persisting path below. Returns the applied theme's name.
            const auto selectedThemeName = theThemeManager.applyTheme(m_lookAndFeel, themeIndex, this);
            m_navigationPanel.sendLookAndFeelChange();
            m_dataViewComponent.sendLookAndFeelChange();
            m_mixEditorComponent.sendLookAndFeelChange();
            m_enhancedPlayer.sendLookAndFeelChange();
            m_statusPanel.sendLookAndFeelChange();
            m_dynamicToolbar.sendLookAndFeelChange();
            CheckboxLookAndFeel::getInstance()->setDefaultLookAndFeel(&m_lookAndFeel);
            return selectedThemeName;
        }

        bool MainComponent::onApplyThemeByIndex(size_t themeIndex)
        {
            const auto &availableThemes = theThemeManager.getAvailableThemes();
            if (themeIndex < availableThemes.size())
            {
                const auto selectedThemeName = applyThemeLive(themeIndex);

                config::TomlBackend backend{g_strConfigFilename};
                config::theSettings.uiSettings.theme.set(selectedThemeName);
                config::theSettings.save(backend);
                return true;
            }
            spdlog::error("Invalid theme index: {}", themeIndex);
            return false;
        }

        void MainComponent::onShowThemeDialog()
        {
            const auto openingIndex = theThemeManager.getCurrentThemeIndex();
            ThemeBrowserDialog::launch(this,
                theThemeManager.getAvailableThemes(),
                openingIndex,
                [this](size_t i) { applyThemeLive(i); },     // preview (no persist)
                [this](size_t i) { onApplyThemeByIndex(i); }, // commit (apply + persist)
                [this](size_t i) { applyThemeLive(i); });    // restore opening theme (no persist)
        }

        bool MainComponent::onShowMaintenanceDialog()
        {
            // Show options dialog first
            DatabaseMaintenanceDialog::show(this, [this](bool accepted, const DatabaseMaintenanceDialog::Options& options)
            {
                if (!accepted)
                {
                    return;  // User cancelled
                }

                // Handle restore from backup (exclusive operation)
                if (options.restoreFromBackup && !options.backupToRestore.empty())
                {
                    performDatabaseRestore(options.backupToRestore);
                    return;
                }

                // Nothing to do if no options selected
                if (!options.backupBeforeMaintenance && !options.runPerformanceOperations)
                {
                    return;
                }

                // Create maintenance task with selected options
                class DatabaseMaintenanceTask final : public ILongRunningTask
                {
                public:
                    DatabaseMaintenanceTask(bool doBackup, bool doPerformance)
                        : ILongRunningTask{"Performing Database Maintenance", true}
                        , m_doBackup{doBackup}
                        , m_doPerformance{doPerformance}
                    {
                    }

                    void run(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel) override
                    {
                        if (!theBackgroundTaskService.pause())
                        {
                            spdlog::warn("DatabaseMaintenanceTask: Background service did not pause in time, proceeding anyway");
                        }

                        // Step 1: Backup if requested
                        if (m_doBackup)
                        {
                            progressCb(0, "Creating database backup...");
                            spdlog::info("Creating database backup before maintenance...");

                            auto dbPath = expandPath(config::theSettings.database.filename.get());
                            database::DatabaseBackupManager backupManager;
                            // Force create a backup (user explicitly requested it)
                            // dryRunPruning is the inverse of enablePruning setting
                            const bool dryRunPruning = !config::theSettings.backupSettings.enablePruning.get();
                            backupManager.performBackupCheck(config::theSettings, dbPath, false, dryRunPruning, true);

                            if (shouldCancel)
                            {
                                completionCb(false, "Database maintenance cancelled.");
                                theBackgroundTaskService.resume();
                                return;
                            }
                        }

                        // Step 2: Run performance operations if requested
                        if (m_doPerformance)
                        {
                            auto maintenanceProgressCb = [&progressCb, this](int percentComplete, const std::string &statusMessage)
                            {
                                // Adjust progress if we did backup first
                                const int adjustedPercent = m_doBackup ? (10 + (percentComplete * 90 / 100)) : percentComplete;
                                progressCb(adjustedPercent, statusMessage);
                            };

                            const bool success = theTrackLibrary.runMaintenanceTasks(shouldCancel, maintenanceProgressCb);

                            if (shouldCancel)
                            {
                                completionCb(false, "Database maintenance cancelled.");
                            }
                            else if (success)
                            {
                                completionCb(true, "Database maintenance completed successfully.");
                            }
                            else
                            {
                                completionCb(false, "Database maintenance failed.");
                            }
                        }
                        else
                        {
                            completionCb(true, "Database backup completed successfully.");
                        }

                        theBackgroundTaskService.resume();
                    }

                private:
                    bool m_doBackup;
                    bool m_doPerformance;
                };

                auto *task = new DatabaseMaintenanceTask{options.backupBeforeMaintenance, options.runPerformanceOperations};
                TaskDialog::launch("Database Maintenance", task, TaskDialog::AutoCloseMode::NoAutoClose, 500, this);
                task->release(REFCOUNT_DEBUG_ARGS);
            });

            return true;
        }

        void MainComponent::performDatabaseRestore(const std::filesystem::path& backupPath)
        {
            class DatabaseRestoreTask final : public ILongRunningTask
            {
            public:
                DatabaseRestoreTask(const std::filesystem::path& backup)
                    : ILongRunningTask{"Restoring Database from Backup", false}
                    , m_backupPath{backup}
                {
                }

                void run(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool>& /*shouldCancel*/) override
                {
                    if (!theBackgroundTaskService.pause())
                    {
                        spdlog::error("RestoreDatabaseTask: Background service did not pause in time, aborting restore");
                        completionCb(false, "Could not pause background tasks. Please try again.");
                        return;
                    }

                    progressCb(10, "Closing database...");
                    spdlog::info("Restoring database from backup: {}", m_backupPath.string());

                    // Get current database path
                    auto dbPath = expandPath(config::theSettings.database.filename.get());

                    progressCb(30, "Copying backup file...");

                    try
                    {
                        // Close the database first
                        theTrackLibrary.shutdown();

                        // Copy backup over current database
                        std::filesystem::copy_file(m_backupPath, dbPath,
                            std::filesystem::copy_options::overwrite_existing);

                        progressCb(70, "Reopening database...");

                        // Reopen the database
                        if (theTrackLibrary.initialise(dbPath))
                        {
                            progressCb(100, "Restore complete.");
                            completionCb(true, "Database restored successfully.");
                        }
                        else
                        {
                            completionCb(false, "Failed to reopen database after restore.");
                        }
                    }
                    catch (const std::exception& e)
                    {
                        spdlog::error("Database restore failed: {}", e.what());
                        completionCb(false, std::string("Restore failed: ") + e.what());
                    }

                    theBackgroundTaskService.resume();
                }

            private:
                std::filesystem::path m_backupPath;
            };

            auto* task = new DatabaseRestoreTask{backupPath};
            juce::Component::SafePointer<MainComponent> safeThis = this;
            TaskDialog::launch("Database Restore", task, TaskDialog::AutoCloseMode::NoAutoClose, 400, this,
                [safeThis](bool success)
                {
                    if (!success)
                        return;
                    // Schedule UI reinitialization on the message thread
                    // This is called after dialog closes (on success or close button)
                    juce::MessageManager::callAsync([safeThis]()
                    {
                        if (safeThis)
                            safeThis->reinitializeAfterRestore();
                    });
                });
            task->release(REFCOUNT_DEBUG_ARGS);
        }

        void MainComponent::reinitializeAfterRestore()
        {
            // Only reinitialize if the database was successfully restored
            if (!theTrackLibrary.isInitialised())
            {
                spdlog::warn("Database not initialized after restore - UI not reinitialized");
                return;
            }

            spdlog::info("Reinitializing UI after database restore...");

            // 1. Stop any playback
            m_playbackController.stop();

            // 2. Clear the current node reference
            if (m_currentNode)
            {
                m_currentNode->release(REFCOUNT_DEBUG_ARGS);
                m_currentNode = nullptr;
            }

            // 3. Clear the DataViewComponent's current node
            m_dataViewComponent.setCurrentNode(nullptr);

            // 4. Reinitialize the navigation tree (this releases old root and creates new one)
            if (!m_navigationTree.initialize())
            {
                spdlog::error("Failed to reinitialize navigation tree after restore");
                m_statusPanel.getStatusBar().postMessage("Failed to reinitialize navigation - please restart", true);
                return;
            }

            // 5. Update track count status
            updateTrackCountStatus();

            // 6. Post success message
            m_statusPanel.getStatusBar().postMessage("Database restored successfully", false);
            spdlog::info("UI reinitialized after database restore");
        }

        void MainComponent::showMarkerDialog(TrackId trackId, std::chrono::milliseconds position, bool isNewMarker)
        {
            auto &markerManager = theTrackLibrary.getMarkerManager();

            // markerManager is a reference, so it's always valid

            auto *dialog = new MarkerEditDialog{};

            if (isNewMarker)
            {
                // Set up for new marker
                dialog->setupForNewMarker(position);

                dialog->onSave = [this, dialog, &markerManager, trackId, position]()
                {
                    const auto comment = dialog->getComment().toStdString();
                    if (comment.empty())
                    {
                        m_statusPanel.getStatusBar().postMessage("Marker comment cannot be empty", true);
                        return;
                    }

                    MarkerId newMarkerId;
                    const auto result = markerManager.createMarker(trackId, position, comment, newMarkerId);

                    if (result == MarkerResult::Success)
                    {
                        spdlog::info("Created marker {} for track {} at {}ms", newMarkerId, trackId, position.count());
                        m_statusPanel.getStatusBar().postMessage("Marker created", false);

                        // Reload markers in the player
                        const auto markers = markerManager.getMarkersForTrack(trackId);
                        m_enhancedPlayer.setMarkers(markers);

                        // Close the dialog
                        if (auto *window = dialog->findParentComponentOfClass<juce::DialogWindow>())
                        {
                            window->exitModalState(0);
                        }
                    }
                    else
                    {
                        spdlog::error("Failed to create marker: result={}", static_cast<int>(result));
                        m_statusPanel.getStatusBar().postMessage("Failed to create marker", true);
                    }
                };
            }
            else
            {
                // Find existing marker at this position
                const auto markers = markerManager.getMarkersForTrack(trackId);
                const auto markerIt = std::find_if(markers.begin(),
                    markers.end(),
                    [position](const auto &m)
                    {
                        return m.position == position;
                    });

                if (markerIt == markers.end())
                {
                    spdlog::error("No marker found at position {}ms", position.count());
                    delete dialog;
                    return;
                }

                const auto marker = *markerIt;
                dialog->setupForExistingMarker(marker);

                // Save callback
                dialog->onSave = [this, dialog, &markerManager, marker, trackId]()
                {
                    const auto newComment = dialog->getComment().toStdString();
                    if (newComment.empty())
                    {
                        m_statusPanel.getStatusBar().postMessage("Marker comment cannot be empty", true);
                        return;
                    }

                    const auto result = markerManager.updateMarker(marker.markerId, newComment);

                    if (result == MarkerResult::Success)
                    {
                        spdlog::info("Updated marker {}", marker.markerId);
                        m_statusPanel.getStatusBar().postMessage("Marker updated", false);

                        // Reload markers in the player
                        const auto markers = markerManager.getMarkersForTrack(trackId);
                        m_enhancedPlayer.setMarkers(markers);

                        // Close the dialog
                        if (auto *window = dialog->findParentComponentOfClass<juce::DialogWindow>())
                        {
                            window->exitModalState(0);
                        }
                    }
                    else
                    {
                        spdlog::error("Failed to update marker: result={}", static_cast<int>(result));
                        m_statusPanel.getStatusBar().postMessage("Failed to update marker", true);
                    }
                };

                // Delete callback
                dialog->onDelete = [this, dialog, &markerManager, marker, trackId]()
                {
                    const auto result = markerManager.deleteMarker(marker.markerId);

                    if (result == MarkerResult::Success)
                    {
                        spdlog::info("Deleted marker {}", marker.markerId);
                        m_statusPanel.getStatusBar().postMessage("Marker deleted", false);

                        // Reload markers in the player
                        const auto markers = markerManager.getMarkersForTrack(trackId);
                        m_enhancedPlayer.setMarkers(markers);

                        // Close the dialog
                        if (auto *window = dialog->findParentComponentOfClass<juce::DialogWindow>())
                        {
                            window->exitModalState(0);
                        }
                    }
                    else
                    {
                        spdlog::error("Failed to delete marker: result={}", static_cast<int>(result));
                        m_statusPanel.getStatusBar().postMessage("Failed to delete marker", true);
                    }
                };
            }

            // Cancel callback (common for both new and existing)
            dialog->onCancel = [dialog]()
            {
                if (auto *window = dialog->findParentComponentOfClass<juce::DialogWindow>())
                {
                    window->exitModalState(0);
                }
            };

            // Show the dialog
            juce::DialogWindow::LaunchOptions launchOptions;
            launchOptions.content.setOwned(dialog);
            launchOptions.dialogTitle = isNewMarker ? "New Marker" : "Edit Marker";
            launchOptions.componentToCentreAround = this;
            launchOptions.escapeKeyTriggersCloseButton = true;
            launchOptions.resizable = false;
            launchOptions.launchAsync();
        }

        void MainComponent::onShowMixEditor()
        {
            lastKnownViewTypeForMixes = MainViewType::MixEditor;
            handleNodeSelection(nullptr, true);
        }

        void MainComponent::onShowTrackEditor()
        {
            lastKnownViewTypeForMixes = MainViewType::DataView;
            handleNodeSelection(nullptr, true);
        }

        void MainComponent::stopMixPlayback()
        {
            if (m_playbackController.isPlaying())
            {
                m_playbackController.stop();
            }
        }

        bool MainComponent::isTrackEditorInMixView() const
        {
            // We're in track editor view for a mix if:
            // 1. We're in DataView mode (not MixEditor)
            // 2. The current node is a specific mix (3rd level under Mixes root)
            if (m_currentMainView != MainViewType::DataView || m_currentNode == nullptr)
            {
                return false;
            }

            const auto nodePath = getNodePath(m_currentNode);
            // Path should be: [0] = Root, [1] = Mixes, [2] = Specific Mix
            return nodePath.size() == 3 && nodePath[1]->getName() == getMixesRootNodeName();
        }

        void MainComponent::updateTrackCountStatus()
        {
            if (!m_currentNode)
            {
                m_statusPanel.getStatusBar().setInfoMessage("");
                return;
            }

            if (m_currentMainView == MainViewType::MixEditor)
            {
                if (const auto* mixNode = dynamic_cast<const database::MixNode*>(m_currentNode))
                {
                    m_statusPanel.getStatusBar().setInfoMessage(formatMixProgressStatus(mixNode->getMixInfo()));
                    return;
                }
            }

            // Try album stats first (for Albums node)
            database::AlbumAggregateStats albumStats;
            if (m_currentNode->getAlbumAggregateStats(albumStats))
            {
                std::string statusMessage = formatAlbumAggregateStats(albumStats);

                // Check if we're in DataView and have album selections
                if (m_currentMainView == MainViewType::DataView)
                {
                    database::AlbumAggregateStats selectionStats;
                    if (m_dataViewComponent.getAlbumSelectionStats(selectionStats))
                    {
                        // Append selection stats
                        statusMessage += std::format(" ({} SELECTED)",
                            formatAlbumAggregateStats(selectionStats));
                    }
                }

                m_statusPanel.getStatusBar().setInfoMessage(statusMessage);
                return;
            }

            // Try track stats (for most other nodes)
            database::AggregateStats trackStats;
            if (m_currentNode->getAggregateStats(trackStats))
            {
                std::string statusMessage = formatAggregateStats(trackStats);

                // Check if we're in DataView and have track selections
                if (m_currentMainView == MainViewType::DataView)
                {
                    database::AggregateStats selectionStats;
                    if (m_dataViewComponent.getSelectionStats(selectionStats))
                    {
                        // Append selection stats
                        statusMessage += std::format(" ({} SELECTED)",
                            formatAggregateStats(selectionStats));
                    }
                }

                m_statusPanel.getStatusBar().setInfoMessage(statusMessage);
                return;
            }

            // Fallback to just showing node name
            m_statusPanel.getStatusBar().setInfoMessage(m_currentNode->getName());
        }

        void MainComponent::showEqualizerWindow()
        {
            // Get current settings from playback controller
            auto currentSettings = m_playbackController.getCurrentEQSettings();

            EqualizerDialog::showEqualizerDialog(
                this,
               [this](const audio::model::EQSettings &settings)
                {
                    // Update the playback controller when settings change
                    m_playbackController.updateMasterEQ(settings);

                    // Update our enabled state and save to config
                    m_equalizerEnabled = settings.isActive;
                    config::theSettings.audioSettings.equalizerBypassed.set(!settings.isActive);
                },
                currentSettings);
        }

        void MainComponent::toggleEqualizerWindow()
        {
            // SingletonDialog handles visibility toggling automatically
            showEqualizerWindow();
        }

        void MainComponent::toggleEqualizerEnabled()
        {
            m_equalizerEnabled = !m_equalizerEnabled;

            // Save to config (inverted: bypassed = !enabled)
            config::theSettings.audioSettings.equalizerBypassed.set(!m_equalizerEnabled);

            // Get current EQ settings from the dialog if it exists
            audio::model::EQSettings settings;
            // The dialog will handle its own settings, we just need to update the bypass state
            settings.isActive = m_equalizerEnabled;
            m_playbackController.updateMasterEQ(settings);

            // Update status bar
            m_statusPanel.getStatusBar().setInfoMessage(m_equalizerEnabled ? "Equalizer enabled" : "Equalizer bypassed");
        }

        void MainComponent::showReverbWindow()
        {
            // Get current settings from playback controller
            auto currentSettings = m_playbackController.getCurrentReverbSettings();

            ReverbDialog::showReverbDialog(
                this,
                [this](const audio::model::ReverbSettings &settings)
                {
                    // Update the playback controller when settings change
                    m_playbackController.updateMasterReverb(settings);

                    // Update our enabled state and save to config
                    m_reverbEnabled = settings.isActive;
                    config::theSettings.audioSettings.reverbBypassed.set(!settings.isActive);
                },
                currentSettings);
        }

        void MainComponent::toggleReverbWindow()
        {
            // SingletonDialog handles visibility toggling automatically
            showReverbWindow();
        }

        void MainComponent::toggleReverbEnabled()
        {
            m_reverbEnabled = !m_reverbEnabled;

            // Save to config (inverted: bypassed = !enabled)
            config::theSettings.audioSettings.reverbBypassed.set(!m_reverbEnabled);

            // Get current reverb settings from the dialog if it exists
            audio::model::ReverbSettings settings;
            // The dialog will handle its own settings, we just need to update the bypass state
            settings.isActive = m_reverbEnabled;
            m_playbackController.updateMasterReverb(settings);

            // Update status bar
            m_statusPanel.getStatusBar().setInfoMessage(m_reverbEnabled ? "Reverb enabled" : "Reverb bypassed");
        }

        void MainComponent::showMasterEffectsWindow()
        {
            auto *component = new PluginChainEditor{};
            SingletonComponentDialog::showComponent("MasterEffects",
                "Master Effects",
                component,
                this,
                false);
        }

        void MainComponent::toggleMasterEffectsBypass()
        {
            const auto bypassed = !audio::theMasterPluginChain.isGlobalBypassed();
            audio::theMasterPluginChain.setGlobalBypassed(bypassed);
            m_statusPanel.getStatusBar().setInfoMessage(bypassed ? "Master effects bypassed" : "Master effects enabled");
        }

        void MainComponent::toggleVisualizer()
        {
            m_visualizerVisible = !m_visualizerVisible;
            spdlog::info("toggleVisualizer: setting visible to {}", m_visualizerVisible);

            m_visualizer.setVisible(m_visualizerVisible);

            if (m_visualizerVisible)
            {
                spdlog::info("toggleVisualizer: starting visualizer");
                m_visualizer.start();
            }
            else
            {
                spdlog::info("toggleVisualizer: stopping visualizer");
                m_visualizer.stop();
            }

            // Trigger relayout
            resized();
        }

        void MainComponent::setVisualizerPlacement(config::VisualizerPlacement placement)
        {
            if (m_visualizerPlacement != placement)
            {
                m_visualizerPlacement = placement;

                // Save to config
                config::theSettings.uiSettings.visualizerPlacement.set(static_cast<int>(placement));
                config::TomlBackend backend{g_strConfigFilename};
                config::theSettings.save(backend);

                spdlog::info("Visualizer placement changed to {}", static_cast<int>(placement));

                // Trigger relayout
                resized();
            }
        }

    } // namespace ui
} // namespace jucyaudio
