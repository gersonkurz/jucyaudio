#pragma once

#include <filesystem>
#include <Audio/AudioLibrary.h>
#include <Audio/Includes/ActiveExportSettings.h>
#include <Database/Includes/INavigationNode.h>
#include <Database/Nodes/RootNode.h>
#include <Database/TrackLibrary.h>
#include <UI/CreateWorkingSetDialogComponent.h>
#include <UI/DataViewComponent.h>
#include <UI/DividerComponent.h>
#include <UI/DynamicToolbarComponent.h>
#include <UI/EnhancedPlayerComponent.h>
#include <UI/MainPlaybackAndStatusComponent.h>
#include <UI/MenuManager.h>
#include <UI/MenuPresenter.h>
#include <UI/MixEditorComponent.h>
#include <UI/NavigationPanelComponent.h>
#include <UI/NavigationTree.h>
#include <UI/PlaybackController.h>
#include <UI/Settings.h>
#include <UI/ThemeManager.h>
#include <UI/TimerMultiplexer.h>
#include <UI/JucyLookAndFeel.h>
#include <UI/Visualizer/ProjectMComponent.h>
#include <Audio/AudioVisualizerFIFO.h>
#include <Utils/UiUtils.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
        using namespace database;

        MainViewType determineMainViewType(const INavigationNode *node);
        MainViewType getLastKnownMainViewType();

        extern std::string g_strConfigFilename;

        class MainComponent : public juce::AudioAppComponent, 
                              public MenuPresenter, 
                              public juce::Timer, 
                              public juce::ChangeListener
        {
        public:
            MainComponent(juce::ApplicationCommandManager &commandManager);
            ~MainComponent() override;

            void paint(juce::Graphics &g) override;
            void resized() override;

            void timerCallback() override;
            void changeListenerCallback(juce::ChangeBroadcaster *source) override;

            // Keyboard handling for media keys
            bool keyPressed(const juce::KeyPress &key) override;

            void adjustSplitterPosition(int deltaX);

            void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
            void getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;
            void releaseResources() override;
            
            // Check and update audio device if needed
            void checkAndUpdateAudioDevice();

            // Getter for DividerComponent to know initial nav panel width
            int getCurrentNavPanelWidth() const
            {
                return m_navPanelWidth;
            }

            // Method called by DividerComponent during drag
            void updateNavPanelWidthFromDrag(int originalNavPanelWidthAtDragStart, int dragDeltaX);

            // @brief Helper method to check if we're in track editor view for a mix
            // @return true if the current main view is MixEditor, false otherwise
            bool isTrackEditorInMixView() const;

            // @brief Stop mix playback if currently playing
            void stopMixPlayback();

            // Node-Centric Command Architecture support
            void navigateToNode(INavigationNode *node)
            {
                handleNodeSelection(node, false, true);
            }
            void playDataRow(RowIndex_t rowIndex);
            void playAllFromRow(RowIndex_t startRow);  // Play all tracks starting from this row (playlist mode)
            bool navigateToFolder(FolderId folderId);

            /// @brief Re-check these tracks' files and correct is_missing.
            /// Public alongside the other entry points DataViewComponent calls: activating a row that is
            /// flagged missing lands here instead of trying to play it.
            void checkFilesForTracks(std::vector<TrackInfo> tracks);

            // Update status bar with track count and statistics
            void updateTrackCountStatus();

        private:
            friend class MainPlaybackAndStatusComponent;
            void handleNodeSelection(INavigationNode *selectedNode, bool forceDisplaySwitch = false, bool syncNavigationTree = false);
            void handleFilterChange(const juce::String &newFilterText);
            void handleNodeActionFromToolbar(DataAction action);
            void handleNodeActionFromNavigationPanel(INavigationNode *selectedNode, DataAction action);
            void handleRowActionFromDataView(RowIndex_t rowIndex, DataAction action, const juce::Point<int> &screenPos);
            void createMix();

            // Media key actions
            void playNextTrack();
            void playPreviousTrack();

            // --- export mix functionality ---
            void onExportMix(INavigationNode *selectedNode);
            void onBatchExport();
            void onRemoveDuplicates(INavigationNode *selectedNode);
            void onExportMixSettingsReceived(const MixInfo &mixInfo, const audio::ActiveExportSettings &settings);
            std::unique_ptr<juce::FileChooser> m_activeFileChooser;

            void requestPlayOrPlaySelection();
            void syncPlaybackUIToControllerState();

            void seekToTimelinePosition(double timePosition);
            void removeTrackFromMix(TrackId trackId);

            // menu management --------------------------------
            bool onShowScanDialog();
            bool onShowMaintenanceDialog();
            void performDatabaseRestore(const std::filesystem::path& backupPath);
            void reinitializeAfterRestore();
            bool onShowConfigureColumnsDialog();
            bool onShowAboutDialog();
            bool onApplyThemeByIndex(size_t themeIndex);
            std::string applyThemeLive(size_t themeIndex); // apply to the whole UI without persisting
            void onShowThemeDialog();

            // Helper to check if a DataAction is available for current node
            bool isActionAvailable(DataAction action) const;

            // working set management -------------------------------
            bool createWorkingSet();
            bool createWorkingSetFromTrackInfos(std::vector<TrackInfo> trackInfos);
            void onCreateWorkingSetFromTrackInfosCallback(const juce::String &name, WorkingSetId targetWsId, std::vector<TrackInfo> trackInfos);
            bool createWorkingSetFromNode(const INavigationNode *node);
            void onCreateWorkingSetFromNodeCallback(const juce::String &name, WorkingSetId targetWsId, const INavigationNode *node);
            void onCommonCreateWorkingSetCallback(bool success, const WorkingSetInfo &workingSetInfo);
            void onMixCreatedCallback(bool success, const MixInfo &mixInfo);
            bool onHandleCreateWorkingSetDialog(int64_t trackCount, OnCreateWorkingSetCallback callback);

            void onRunBpmAnalysis(INavigationNode *node);
            void onRunBpmAnalysisForSelectedRows();

            /// @brief Re-check the selected rows' files and correct is_missing.
            void onCheckFilesForSelectedRows();

            void showBadFilesDialog(const std::vector<TrackInfo> &badFiles);

            void onEditWorkingSetMetadata(INavigationNode *node);
            void onEditMixMetadata(INavigationNode *node);
            void onUnlockMixForEditing(INavigationNode *node);
            void handleCloneMix(const database::MixInfo& mixInfo);
            juce::String generateCloneName(const std::string& originalName);
            void onShowInFolder(RowIndex_t rowIndex);
            void onCopyToClipboard(RowIndex_t rowIndex);
            bool showTrackInLibrary(TrackId trackId);
            void showTrackDetailsDialog(TrackId trackId);
            void launchTrackDetailsDialog(std::vector<database::TrackInfo> tracks);

            // Equalizer management
            void showEqualizerWindow();
            void toggleEqualizerWindow();
            void toggleEqualizerEnabled();

            // Reverb management
            void showReverbWindow();
            void toggleReverbWindow();
            void toggleReverbEnabled();

            // Master effects chain management
            void showMasterEffectsWindow();
            void toggleMasterEffectsBypass();

            // Visualizer management
            void toggleVisualizer();
            void setVisualizerPlacement(config::VisualizerPlacement placement);
            bool isVisualizerVisible() const { return m_visualizerVisible; }

            audio::AudioLibrary m_audioLibrary;
            juce::ApplicationCommandManager &m_commandManager;

            // UI Child Components

            MainViewType m_currentMainView{MainViewType::DataView};
            juce::Component *m_currentMainViewComponent{nullptr};
            DataViewComponent m_dataViewComponent{*this};
            MixEditorComponent m_mixEditorComponent;
            DynamicToolbarComponent m_dynamicToolbar;
            NavigationPanelComponent m_navigationPanel;
            NavigationTree m_navigationTree;

            DividerComponent m_verticalDivider;

            // Audio components for waveform display
            juce::AudioFormatManager m_audioFormatManager;
            juce::AudioThumbnailCache m_audioThumbnailCache{200}; // Cache for 200 thumbnails - enough for large mixes

            PlaybackController m_playbackController;
            EnhancedPlayerComponent m_enhancedPlayer; // Direct member object
            MainPlaybackAndStatusComponent m_statusPanel;
            JucyLookAndFeel m_lookAndFeel;

            // Visualizer components
            audio::AudioVisualizerFIFO m_visualizerFIFO;
            ProjectMComponent m_visualizer;
            bool m_visualizerVisible{false};
            config::VisualizerPlacement m_visualizerPlacement{config::VisualizerPlacement::Bottom};

            // Unified timer system
            TimerMultiplexer m_timerMultiplexer;

            // DSP effects state
            bool m_equalizerEnabled{true}; // Whether EQ processing is active
            bool m_reverbEnabled{true}; // Whether reverb processing is active

            std::filesystem::path getThemesDirectoryPath() const;

            // Layout parameters
            int m_navPanelWidth{250};        // << NEW: Current width of the navigation panel
            const int m_dividerThickness{5}; // << NEW: Thickness of the divider bar

            // Marker handling
            void showMarkerDialog(TrackId trackId, std::chrono::milliseconds position, bool isNewMarker);
            INavigationNode *m_currentNode{nullptr};

            enum CommandIDs
            {
                cmd_ScanFolders = 1,
                cmd_AddFolderToLibrary,
                cmd_DatabaseMaintenance,
                cmd_Options_ConfigureColumns,
                cmd_About,
                cmd_Exit,
                cmd_ShowEqualizer,
                cmd_ToggleEqualizerEnabled,
                cmd_ToggleVisualizer,
                cmd_VisualizerPlacementBottom,
                cmd_VisualizerPlacementLeft,
                cmd_VisualizerPlacementRight,
            };

            void onDataActionDelete(INavigationNode *selectedNode);

            void onDataActionDeleteConfirmed(INavigationNode *selectedNode, int result);

            void onDataActionRemoveNamedObjects();

            // @brief Delete tracks from the library (with option to delete files from hard drive)
            void onDeleteTracksFromLibrary();

            // @brief Called when you want to switch the mix-view to "Mix Editor" mode, that is: waveform view
            void onShowMixEditor();

            // @brief Called when you want to switch the mix-view to "Data View" mode, that is: table view
            void onShowTrackEditor();

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
        };

    } // namespace ui
} // namespace jucyaudio
