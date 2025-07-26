#pragma once

#include <Audio/AudioLibrary.h>
#include <Database/Includes/INavigationNode.h>
#include <Database/Nodes/RootNode.h>
#include <Database/TrackLibrary.h>
#include <UI/DataViewComponent.h>
#include <UI/DividerComponent.h>
#include <UI/DynamicToolbarComponent.h>
#include <UI/MainPlaybackAndStatusComponent.h>
#include <UI/MenuManager.h>
#include <UI/NavigationTree.h>
#include <UI/MenuPresenter.h>
#include <UI/NavigationPanelComponent.h>
#include <UI/MixEditorComponent.h>
#include <UI/PlaybackController.h>
#include <UI/PlaybackToolbarComponent.h>
#include <UI/EnhancedPlayerComponent.h>
#include <UI/CreateWorkingSetDialogComponent.h>
#include <UI/ThemeManager.h>
#include <Utils/UiUtils.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace jucyaudio
{
    namespace ui
    {
        using namespace database;


        MainViewType determineType(const INavigationNode *node);

        extern std::string g_strConfigFilename;
        class MainComponent : public juce::AudioAppComponent, public MenuPresenter, public juce::Timer, public juce::ChangeListener
        {
        public:            
            MainComponent(juce::ApplicationCommandManager &commandManager);
            ~MainComponent() override;

            void paint(juce::Graphics &g) override;
            void resized() override;

            void timerCallback() override;
            void changeListenerCallback(juce::ChangeBroadcaster *source) override;

            void adjustSplitterPosition(int deltaX);

            void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
            void getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;
            void releaseResources() override;

            // Getter for DividerComponent to know initial nav panel width
            int getCurrentNavPanelWidth() const
            {
                return m_navPanelWidth;
            }

            // Method called by DividerComponent during drag
            void updateNavPanelWidthFromDrag(int originalNavPanelWidthAtDragStart, int dragDeltaX);

        private:
            friend class MainPlaybackAndStatusComponent;
            void handleNodeSelection(INavigationNode *selectedNode);
            void handleFilterChange(const juce::String &newFilterText);
            void handleNodeActionFromToolbar(DataAction action);
            void handleNodeActionFromNavigationPanel(INavigationNode *selectedNode, DataAction action);
            void handleRowActionFromDataView(RowIndex_t rowIndex, DataAction action, const juce::Point<int> &screenPos);

            void playDataRow(RowIndex_t rowIndex);
            void createMix();




            // --- export mix functionality ---
            void onExportMix(INavigationNode *selectedNode);
            void onExportMixFileChooserModalDismissed(const juce::FileChooser &chooser, MixInfo mixInfo);
            std::unique_ptr<juce::FileChooser> m_activeFileChooser;

            void requestPlayOrPlaySelection();
            void syncPlaybackUIToControllerState();
            void playFileFromPosition(const juce::File &audioFile, double startPosition);
            void seekToTimelinePosition(double timePosition);
            void removeTrackFromMix(TrackId trackId);

            // menu management --------------------------------
            bool onShowScanDialog();
            bool onShowMaintenanceDialog();
            bool onBuildVirtualFolders();
            bool onShowConfigureColumnsDialog();
            bool onShowAboutDialog();
            bool onApplyThemeByIndex(size_t themeIndex);

            // working set management -------------------------------
            bool createWorkingSet();
            bool createWorkingSetFromTrackIds(std::vector<TrackId> trackIds);
            void onCreateWorkingSetFromTrackIdsCallback(const juce::String &name, std::vector<TrackId> trackIds);
            bool createWorkingSetFromNode(const INavigationNode *node);
            void onCreateWorkingSetFromNodeCallback(const juce::String &name, const INavigationNode *node);
            void onCommonCreateWorkingSetCallback(bool success, const WorkingSetInfo &workingSetInfo);
            void onMixCreatedCallback(bool success, const MixInfo &mixInfo);
            bool onHandleCreateWorkingSetDialog(int64_t trackCount, OnCreateWorkingSetCallback callback);

            void onRunBpmAnalysis(INavigationNode* node);
            void onRunBpmAnalysisForSelectedRows();
            void showBadFilesDialog(const std::vector<TrackInfo>& badFiles);

            void onEditWorkingSetMetadata(INavigationNode *node);

            audio::AudioLibrary m_audioLibrary;
            juce::ApplicationCommandManager &m_commandManager;

            // UI Child Components
            DynamicToolbarComponent m_dynamicToolbar;
            NavigationPanelComponent m_navigationPanel;
            NavigationTree m_navigationTree;

            MainViewType m_currentMainView{MainViewType::DataView};
            juce::Component *m_currentMainViewComponent{nullptr};
            DataViewComponent m_dataViewComponent;
            MixEditorComponent m_mixEditorComponent;

            DividerComponent m_verticalDivider;
            
            // Audio components for waveform display
            juce::AudioFormatManager m_audioFormatManager;
            juce::AudioThumbnailCache m_audioThumbnailCache{10}; // Cache for 10 thumbnails

            PlaybackToolbarComponent m_hiddenPlaybackToolbar; // Hidden, just for PlaybackController
            PlaybackController m_playbackController;
            EnhancedPlayerComponent m_enhancedPlayer; // Direct member object
            MainPlaybackAndStatusComponent m_statusPanel;
            juce::LookAndFeel_V4 m_lookAndFeel;

            std::filesystem::path getThemesDirectoryPath() const;

            // Layout parameters
            int m_navPanelWidth{250};        // << NEW: Current width of the navigation panel
            const int m_dividerThickness{5}; // << NEW: Thickness of the divider bar

            RootNode *m_rootNavigationNode{nullptr};
            
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
            };

            struct DeleteContext final
            {
                // will have an added reference, so you must release it from the callback
                INavigationNode *node{nullptr};
                std::vector<RowIndex_t> selectedRows;
            };

            
            void onDataActionDelete(INavigationNode *selectedNode);
            
            void onDataActionDeleteConfirmed(INavigationNode *selectedNode, int result);

            // @brief Called when you select one or more objects in the DataView and ask to delete them.
            void onDataActionDeleteSelectedObjects();

            // @brief Called when you select one or more tracks in the DataView and ask to remove them.
            void onDataActionRemoveTracks();

            // &brief Called when you confirm the warning dialog in onDataActionRemoveTracks in order to proceed 
            // to the actual removal of the tracks from the current node / mixes from the list of mixes etc.
            void onRemoveRowsFromCurrentNode(DeleteContext *const dc, int result);

            void onDataActionRemoveNamedObjects(std::string_view itemTypeSingular, std::string_view itemTypePlural);

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
        };

    } // namespace ui
} // namespace jucyaudio
