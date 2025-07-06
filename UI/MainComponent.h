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
#include <UI/MenuPresenter.h>
#include <UI/NavigationPanelComponent.h>
#include <UI/PlaybackController.h>
#include <UI/ThemeManager.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
        juce::LookAndFeel_V4::ColourScheme getColourSchemeFromConfig();

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
            void onDeleteSelectedRows(std::vector<RowIndex_t> selectedRows, database::INavigationNode *node, int result);
            void handleNodeSelection(database::INavigationNode *selectedNode);
            void handleFilterChange(const juce::String &newFilterText);
            void handleNodeActionFromToolbar(database::DataAction action);
            void handleNodeAction(database::INavigationNode *selectedNode, database::DataAction action);
            void handleRowActionFromDataView(RowIndex_t rowIndex, database::DataAction action, const juce::Point<int> &screenPos);

            void playDataRow(RowIndex_t rowIndex);
            void deleteSelectedRows();
            void createMix();
            void removeMix(database::INavigationNode *selectedNode);
            void onDoRemoveMix(database::INavigationNode *selectedNode, const database::MixInfo &mixToDelete, int result);

            void requestPlayOrPlaySelection();
            void syncPlaybackUIToControllerState();

            // menu management --------------------------------
            bool onShowScanDialog();
            bool onShowMaintenanceDialog();
            bool onShowConfigureColumnsDialog();
            bool onShowAboutDialog();
            bool onApplyThemeByIndex(size_t themeIndex);

            // working set management -------------------------------
            bool createWorkingSet();
            bool createWorkingSetFromTrackIds(std::vector<database::TrackId> trackIds);
            void onCreateWorkingSetFromTrackIdsCallback(const juce::String &name, std::vector<database::TrackId> trackIds);
            bool createWorkingSetFromNode(const database::INavigationNode *node);
            void onCreateWorkingSetFromNodeCallback(const juce::String &name, const database::INavigationNode *node);
            void onCommonCreateWorkingSetCallback(bool success, const database::WorkingSetInfo &workingSetInfo);
            bool onHandleCreateWorkingSetDialog(int64_t trackCount, std::function<void(const juce::String &)> callback);

            database::TrackLibrary m_trackLibrary;
            audio::AudioLibrary m_audioLibrary;
            juce::ApplicationCommandManager &m_commandManager;

            // UI Child Components
            DynamicToolbarComponent m_dynamicToolbar;
            NavigationPanelComponent m_navigationPanel; // Will be laid out directly
            DataViewComponent m_dataView;               // Will be laid out directly
            DividerComponent m_verticalDivider;

            PlaybackToolbarComponent m_playbackToolbar; // Direct member object
            PlaybackController m_playbackController;
            MainPlaybackAndStatusComponent m_mainPlaybackAndStatusPanel;
            juce::LookAndFeel_V4 m_lookAndFeel;

            std::filesystem::path getThemesDirectoryPath() const;

            // Layout parameters
            int m_navPanelWidth{250};        // << NEW: Current width of the navigation panel
            const int m_dividerThickness{5}; // << NEW: Thickness of the divider bar

            database::RootNode *m_rootNavigationNode{nullptr};
            database::INavigationNode *m_currentSelectedDataNode{nullptr};

            enum CommandIDs
            {
                cmd_ScanFolders = 1,
                cmd_AddFolderToLibrary,
                cmd_DatabaseMaintenance,
                cmd_Options_ConfigureColumns,
                cmd_About,
                cmd_Exit,
            };

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
        };

    } // namespace ui
} // namespace jucyaudio
