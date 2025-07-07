#include <Config/toml_backend.h>
#include <Database/BackgroundService.h>
#include <Database/BackgroundTasks/BpmAnalysis.h>
#include <Database/Nodes/MixNode.h>
#include <Database/Nodes/RootNode.h>
#include <UI/ColumnConfiguratorDialog.h>
#include <UI/CreateMixDialogComponent.h>
#include <UI/CreateWorkingSetDialogComponent.h>
#include <UI/ILongRunningTask.h>
#include <UI/MainComponent.h>
#include <UI/ScanDialogComponent.h>
#include <UI/TaskDialog.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>
#ifndef JUCE_WINDOWS
#include <unistd.h>
#endif

namespace jucyaudio
{
    using namespace database;

    namespace ui
    {
        MainViewType determineType(const database::INavigationNode *node)
        {
            const auto nodePath{getNodePath(node)};
            if(nodePath.size() >= 3)
            {
                if(nodePath[1]->getName() == getMixesRootNodeName())
                {
                    return MainViewType::MixEditor;
                }
            }
            // Add more types as needed
            return MainViewType::DataView; // Default to DataView if no specific type matches
        }

        MainComponent::MainComponent(juce::ApplicationCommandManager &commandManager)
            : MenuPresenter{commandManager},
              m_commandManager{commandManager},
              m_dynamicToolbar{*this}, // Pass *this as MainComponent& owner
              m_navigationPanel{*this},
              m_currentMainView{MainViewType::DataView},
              m_currentMainViewComponent{&m_dataViewComponent},
              m_verticalDivider{*this, true},
              m_playbackController{m_playbackToolbar},
              m_mainPlaybackAndStatusPanel{*this}
        {
            theThemeManager.applyCurrentTheme(m_lookAndFeel, this);

            // --- TrackLibrary Initialization (remains as is) ---
            juce::File appDataDir{juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("jucyaudioApp_Dev")};
            if (!appDataDir.exists())
            {
                appDataDir.createDirectory();
            }
            juce::File dbJuceFile{appDataDir.getChildFile("jucyaudio_library_dev.sqlite")};
            std::filesystem::path dbPath{dbJuceFile.getFullPathName().toStdString()};

            if (theTrackLibrary.initialise(dbPath))
            {
                spdlog::info("TrackLibrary initialised successfully by "
                             "MainComponent for DB: {}",
                             dbPath.string());
            }
            else
            {
                spdlog::error("TrackLibrary FAILED to initialise from "
                              "MainComponent. Error: {}",
                              theTrackLibrary.getLastError());
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Engine Error",
                                                       "TrackLibrary failed to initialize.\nDB Path: " + dbJuceFile.getFullPathName() +
                                                           "\nError: " + juce::String(theTrackLibrary.getLastError()));
            }

            // --- Add and make visible all child components ---
            addAndMakeVisible(m_dynamicToolbar);
            addAndMakeVisible(m_navigationPanel);
            addAndMakeVisible(m_verticalDivider);

            addAndMakeVisible(m_dataViewComponent);
            addChildComponent(m_mixEditorComponent); // also a child but not yet visible

            addAndMakeVisible(m_mainPlaybackAndStatusPanel);

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
                handleNodeAction(selectedNode, dataAction);
            };

            // Playback Toolbar (callbacks from
            // m_mainPlaybackAndStatusPanel.getPlaybackToolbar())
            auto &toolbar = m_mainPlaybackAndStatusPanel.getPlaybackToolbar();
            toolbar.onPlayClicked = [this]
            {
                requestPlayOrPlaySelection();
            };
            toolbar.onPauseClicked = [this]
            {
                m_playbackController.pause();
            }; // Direct call
            toolbar.onStopClicked = [this]
            {
                m_playbackController.stop();
            }; // Direct call
            toolbar.onPositionSeek = [this](double newNormalizedPosition)
            {
                if (m_playbackController.getLengthInSeconds() > 0)
                {
                    m_playbackController.seek(newNormalizedPosition);
                }
            };
            toolbar.onVolumeChanged = [this](float newGain)
            {
                m_playbackController.setGain(newGain);
            };

            // --- Initialize Navigation ---
            m_rootNavigationNode = static_cast<RootNode *>(theTrackLibrary.getRootNavigationNode()); // Returns retained
            if (m_rootNavigationNode)
            {
                m_navigationPanel.setRootNode(m_rootNavigationNode);
                // Optionally select and display the first child of root or root
                // itself if it has data For now, let node selection
                // user-driven.
                // << NEW: Auto-select the first available top-level node >>
                if (m_navigationPanel.getTreeView().getNumRowsInTree() > 0) // Check if TreeView has any visible items
                {
                    // The first visible item is the first child of our
                    // (invisible) root TreeViewItem. The TreeView itself
                    // doesn't directly give us INavigationNode*s. We need to
                    // get the TreeViewItem and then its associated
                    // INavigationNode.

                    // Get the TreeViewItem for the first visible row (index 0
                    // of the TreeView's flat list)
                    if (auto *firstTopLevelTreeViewItem =
                            dynamic_cast<NavigationPanelComponent::NavTreeViewItem *>(m_navigationPanel.getTreeView().getItemOnRow(0)))
                    {
                        // if (jucyaudio::INavigationNode *nodeToSelect = firstTopLevelTreeViewItem->getNode())
                        {
                            // Select this item in the TreeView UI
                            firstTopLevelTreeViewItem->setSelected(true, true);
                            // Note: setSelected might trigger
                            // NavTreeViewItem::itemSelectionChanged, which
                            // calls
                            // NavigationPanelComponent::handleItemSelection,
                            // which calls m_navigationPanel.onNodeSelected,
                            // which calls MainComponent::handleNodeSelection.
                            // So, explicitly calling handleNodeSelection might
                            // be redundant if the above path works. Let's test
                            // if setSelected is enough.

                            // If setSelected doesn't trigger the full callback
                            // chain to MainComponent::handleNodeSelection
                            // (e.g., if onNodeSelected is only for user
                            // clicks), then call it manually:
                            // nodeToSelect->retain(REFCOUNT_DEBUG_ARGS); //
                            // handleNodeSelection expects a retained node
                            // handleNodeSelection(nodeToSelect);
                            // The above line
                            // `firstTopLevelTreeViewItem->setSelected(true,
                            // true);` SHOULD trigger the sequence.
                        }
                    }
                }
            }
            else
            {
                // Handle case where root node isn't available (e.g., show error
                // in status bar)
                m_mainPlaybackAndStatusPanel.setStatusMessage("Error: Could not load navigation.", true);
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
            syncPlaybackUIToControllerState();
            // Initialize volume slider in toolbar from controller's current
            // gain (Assuming PlaybackController has a getGain() or
            // PlaybackToolbarComponent fetches it on init) For now, let
            // PlaybackController::syncUIToPlaybackControllerState handle this
            // via toolbar reference.

            // --- Application Commands and Menu ---
            m_commandManager.registerAllCommandsForTarget(this); // Register commands defined in this class

            auto &menuManager = getManager();

            // 1. Define static menus
            menuManager.registerMenu("File", {// The lambda captures `this` from MainComponent, keeping logic and state together.
                                              {"Scan Folders...",
                                               "...",
                                               [&]()
                                               {
                                                   onShowScanDialog();
                                               },
                                               {{'s', juce::ModifierKeys::commandModifier}},
                                               {}},
                                              {"-"},
                                              {"Database Maintenance...", "...",
                                               [&]()
                                               {
                                                   onShowMaintenanceDialog();
                                               }},
                                              {"-"},
                                              {"Exit",
                                               "...",
                                               [&]()
                                               {
                                                   juce::JUCEApplication::getInstance()->systemRequestedQuit();
                                               },
                                               {{'q', juce::ModifierKeys::commandModifier}},
                                               {}}});

            menuManager.registerMenu("View", {{"Configure Columns...", "...", [&]()
                                               {
                                                   onShowConfigureColumnsDialog();
                                               }}});

            // 2. Define dynamic theme submenu
            std::vector<MenuItem> themeItems;
            const auto &availableThemes = theThemeManager.getAvailableThemes();
            for (size_t i = 0; i < availableThemes.size(); ++i)
            {
                themeItems.push_back({
                    availableThemes[i].name,
                    "Select this theme",
                    [this, i]()
                    {
                        onApplyThemeByIndex(i);
                    }, // Lambda captures index
                    {},
                    true, // isRadioButton
                    [i]()
                    {
                        return theThemeManager.isCurrentIndex(i);
                    } // isTicked lambda
                });
            }
            menuManager.addSubMenu("View", "Theme", themeItems);

            menuManager.registerMenu("Help",
                                     {
                                         {"About...", "...",
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
            startTimerHz(30); // For smooth UI updates (e.g., playback position slider)

            // Initial size
            setSize(1200, 800);

            // Required for AudioAppComponent
            setAudioChannels(0, 2); // Output only

            // Setup the background service (assuming it's a member m_backgroundService)
            database::theBackgroundTaskService.start();

            // Create and register our new BPM analysis task.
            // Note: the service will retain() the task, so we can release our initial reference.
            auto *bpmTask = new database::background_tasks::BpmAnalysis{};
            database::theBackgroundTaskService.registerTask(bpmTask);
            bpmTask->release(REFCOUNT_DEBUG_ARGS);
        }

        MainComponent::~MainComponent()
        {
            database::theBackgroundTaskService.stop();
#if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu(nullptr);
#endif
            // This is important for clean shutdown. It tells all child components
            // to stop using our m_lookAndFeel object before it gets destroyed.
            setLookAndFeel(nullptr);
#ifdef USE_REFCOUNT_DEBUGGING
            for (const auto item : theBaseNodes)
            {
                spdlog::error("MainComponent::~MainComponent - BaseNode still retained: {} at {}", item->getName(), (void *)item);
            }
#endif

            stopTimer();
            shutdownAudio(); // From AudioAppComponent
            // Remove listeners
            if (juce::MessageManager::getInstanceWithoutCreating() != nullptr) // Check if MM still exists
            {
                m_playbackController.getTransportSource().removeChangeListener(this);
                // m_playbackController.removeChangeListener(this);
            }

            // Release retained INavigationNode pointers
            if (m_currentSelectedDataNode)
            {
                m_currentSelectedDataNode->dataNoLongerShowing(); // Good practice
                m_currentSelectedDataNode->release(REFCOUNT_DEBUG_ARGS);
                m_currentSelectedDataNode = nullptr;
            }
            if (m_rootNavigationNode)
            {
                m_rootNavigationNode->release(REFCOUNT_DEBUG_ARGS);
                m_rootNavigationNode = nullptr;
            }
        }

        void MainComponent::paint(juce::Graphics &g)
        {
            // Optionally fill the main background if child components don't
            // cover everything or if there are gaps.
            const auto backgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
            //spdlog::info("MainComponent::paint - juce::ResizableWindow::backgroundColourId colour: '#{}'", backgroundColour.toString().toStdString());
            g.fillAll(backgroundColour);
        }

        // Method called by DividerComponent's mouseDrag
        void MainComponent::updateNavPanelWidthFromDrag(int originalNavPanelWidthAtDragStart, int dragDeltaX)
        {
            int newNavPanelWidth = originalNavPanelWidthAtDragStart + dragDeltaX;

            // Get the bounds of the area available for navPanel, divider, and
            // dataView auto bounds = getLocalBounds();
            /* int toolbarHeight = */ m_dynamicToolbar.getHeight();                 // Assuming toolbar is visible and has a height
            /* int bottomPanelHeight = */ m_mainPlaybackAndStatusPanel.getHeight(); // Assuming panel is visible

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
            tempBounds.removeFromBottom(m_mainPlaybackAndStatusPanel.getHeight());
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
            auto bounds = getLocalBounds();
            // Get actual heights after components are potentially laid out once
            int toolbarHeight = m_dynamicToolbar.isVisible() ? m_dynamicToolbar.getHeight() : 0;
            if (toolbarHeight == 0 && m_dynamicToolbar.isVisible())
                toolbarHeight = 40; // Default if not yet sized

            int bottomPanelHeight = m_mainPlaybackAndStatusPanel.isVisible() ? m_mainPlaybackAndStatusPanel.getHeight() : 0;
            if (bottomPanelHeight == 0 && m_mainPlaybackAndStatusPanel.isVisible())
                bottomPanelHeight = 50; // Default

            m_dynamicToolbar.setBounds(bounds.removeFromTop(toolbarHeight));
            m_mainPlaybackAndStatusPanel.setBounds(bounds.removeFromBottom(bottomPanelHeight));

            auto centralArea = bounds; // This is now the area for nav, divider, data

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

            //m_currentMainViewComponent->setBounds(dataViewX, centralArea.getY(), dataViewWidth, centralArea.getHeight());
            m_dataViewComponent.setBounds(dataViewX, centralArea.getY(), dataViewWidth, centralArea.getHeight());
            m_mixEditorComponent.setBounds(dataViewX, centralArea.getY(), dataViewWidth, centralArea.getHeight());
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
            m_playbackController.prepareToPlay(samplesPerBlockExpected, sampleRate);
            m_mainPlaybackAndStatusPanel.setStatusMessage("Audio device prepared.", false);
        }

        void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill)
        {
            m_playbackController.getNextAudioBlock(bufferToFill);
        }

        void MainComponent::releaseResources()
        {
            m_playbackController.releaseResources();
            m_mainPlaybackAndStatusPanel.setStatusMessage("Audio resources released.", false);
        }

        // --- juce::Timer Override ---
        void MainComponent::timerCallback()
        {
            m_playbackController.onTimerEvent(); // This updates toolbar's time, slider pos etc.
            // Any other periodic UI updates can go here
        }

        // --- juce::ChangeListener Override ---
        void MainComponent::changeListenerCallback(juce::ChangeBroadcaster *source)
        {
            if (source == &m_playbackController.getTransportSource())
            {
                // This typically means the track ended, or playback state
                // changed significantly from the transport source itself (e.g.
                // stopped due to error).
                syncPlaybackUIToControllerState();
            }
            // else if (source == &m_playbackController) { /* Handle other
            // general PlaybackController changes */ }
        }

        // --- UI State Synchronization ---
        void MainComponent::syncPlaybackUIToControllerState()
        {
            // The PlaybackController itself updates its toolbar.
            // This method in MainComponent is mostly a trigger or if
            // MainComponent needs to update other things based on playback
            // state. The old MainComponent called:
            // m_playbackController.syncUIToPlaybackControllerState(m_trackTableView.getSelectedRow()
            // != -1); We need an equivalent for "is a playable item selected in
            // data view?" For now, let's pass true/false based on
            // m_currentSelectedDataNode or if DataView has selection. This
            // boolean was used by PlaybackController to enable/disable play
            // button if nothing is cued.
            bool canPlaySelection = (m_currentSelectedDataNode != nullptr); // Simplistic: if a node is selected.
                                                                            // More accurately: if data view has a selected row
                                                                            // and that row represents a playable track.
            m_playbackController.syncUIToPlaybackControllerState(canPlaySelection);
        }

        // --- Handler Method Stubs / Basic Logic ---
        void MainComponent::handleNodeSelection(INavigationNode *selectedNode) // selectedNode is retained by caller (NavPanel)
        {
            if (m_currentSelectedDataNode == selectedNode)
            {
                if (selectedNode)
                    selectedNode->release(REFCOUNT_DEBUG_ARGS); // Release the new one if it's
                                                                // same as old
                return;
            }

            if (m_currentSelectedDataNode)
            {
                m_currentSelectedDataNode->dataNoLongerShowing();
                m_currentSelectedDataNode->release(REFCOUNT_DEBUG_ARGS);
            }

            const auto currentViewType{m_currentMainView};
            m_currentSelectedDataNode = selectedNode; // Takes ownership of the retained selectedNode

            if (m_currentSelectedDataNode)
            {
                const auto newViewType{determineType(m_currentSelectedDataNode)};

                if(currentViewType != newViewType)
                {
                    m_currentMainViewComponent->setVisible(false);
                    if(newViewType == MainViewType::MixEditor)
                    {
                        m_currentMainViewComponent = &m_mixEditorComponent;
                        m_currentMainView = MainViewType::MixEditor;
                        m_mixEditorComponent.setVisible(true);
                    }
                    else
                    {
                        m_currentMainViewComponent = &m_dataViewComponent;
                        m_currentMainView = MainViewType::DataView;
                        m_dataViewComponent.setVisible(true);
                    }
                }

                // we should use a function to crate a string path here.
                const auto path{getNodePath(m_currentSelectedDataNode)};

                m_currentSelectedDataNode->prepareToShowData();
                m_dynamicToolbar.setCurrentNode(m_currentSelectedDataNode); // Toolbar updates its actions
                if (m_currentMainView == MainViewType::MixEditor)
                {
                    m_mixEditorComponent.loadMix(m_currentSelectedDataNode->getUniqueId()); // Load the mix data
                }
                else
                {
                    m_dataViewComponent.setCurrentNode(m_currentSelectedDataNode); // DataView updates its content source
                    m_dataViewComponent.refreshView();                             // Tell DataView to redraw
                }
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
            }
            syncPlaybackUIToControllerState(); // Update play button enable
                                               // state
        }

        void MainComponent::handleFilterChange(const juce::String &newFilterText)
        {
            if (m_currentSelectedDataNode)
            {
                // Convert juce::String to std::vector<std::string> for
                // setSearchTerms Simple split by space for now.
                std::vector<std::string> searchTerms;
                if (!newFilterText.isEmpty())
                {
                    auto termsArray = juce::StringArray::fromTokens(newFilterText, " ", "\"");
                    termsArray.removeEmptyStrings();
                    for (const auto &term : termsArray)
                    {
                        searchTerms.push_back(term.toStdString());
                    }
                }

                if (m_currentSelectedDataNode->setSearchTerms(searchTerms))
                {
                    if (m_currentMainView == MainViewType::MixEditor)
                    {
                        // mix editor doesn't support filtering, so nothing to see here
                    }
                    else
                    {
                        m_dataViewComponent.refreshView(); // Tell DataView data has changed due to filter
                    }
                }
            }
        }

        void MainComponent::handleNodeActionFromToolbar(DataAction action)
        {
            if (!m_currentSelectedDataNode)
                return;
            handleNodeAction(m_currentSelectedDataNode, action);
        }

        void MainComponent::handleNodeAction(INavigationNode *selectedNode, DataAction action)
        {
            m_mainPlaybackAndStatusPanel.setStatusMessage("Node action: " + juce::String(static_cast<int>(action)), false);

            switch (action)
            {
            case DataAction::CreateWorkingSet:
                createWorkingSet();
                break;
            case DataAction::CreateMix:
                createMix();
                break;
            case DataAction::RemoveMix:
                onRemoveMix(selectedNode);
                break;
            case DataAction::ExportMix:
                onExportMix(selectedNode);
                break;
            case DataAction::None:
            default:
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
            case DataAction::RemoveMix:
            case DataAction::ExportMix:
                spdlog::warn("Unsupported action '{}' for row {}. This should not happen.", static_cast<int>(action), rowIndex);
                break;
            case DataAction::ShowDetails:
                m_mainPlaybackAndStatusPanel.setStatusMessage("Show details for: " + std::to_string(rowIndex), false);
                break;
            case DataAction::EditMetadata:
                m_mainPlaybackAndStatusPanel.setStatusMessage("Edit metadata for: " + std::to_string(rowIndex), false);
                break;
            case DataAction::Delete:
                deleteSelectedRows();
                break;
            case DataAction::None:
            default:
                break;
            }
        }
        // --- Action Execution Method Stubs ---
        void MainComponent::playDataRow(RowIndex_t rowIndex)
        {
            if (!m_currentSelectedDataNode)
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("No node selected for playback.", true);
                return;
            }
            const auto track{m_currentSelectedDataNode->getTrackInfoForRow(rowIndex)};
            if (!track)
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("No track info available for row: " + std::to_string(rowIndex), true);
                return;
            }

            juce::File audioFile{jucePathFromFs(track->filepath)};
            if (audioFile.existsAsFile())
            {
                // uncomment this line, and you get the exceptio
                m_mainPlaybackAndStatusPanel.setStatusMessage(getSafeDisplayText("Playing: " + audioFile.getFileName()), false);
                if (!m_playbackController.loadAndPlayFile(audioFile))
                {
                    m_mainPlaybackAndStatusPanel.setStatusMessage(getSafeDisplayText("Error playing: " + audioFile.getFileName()), true);
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Playback Error",
                                                           "Cannot play file:\n" + audioFile.getFullPathName());
                }
            }
            else
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("Cannot play: " + std::to_string(track->trackId) + " (No path)", true);
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Playback Error",
                                                       "Cannot find audio file for: " + std::to_string(track->trackId));
            }
            syncPlaybackUIToControllerState();
        }

        void MainComponent::onDeleteSelectedRows(std::vector<RowIndex_t> selectedRows, INavigationNode *node, int result)
        {
            if (result == 1) // User clicked "Delete"
            {
                for (const auto rowIndex : selectedRows)
                {
                    spdlog::info("Deleting row index: {}", rowIndex);
                    node->removeObjectAtRow(rowIndex);
                }
                m_navigationPanel.refreshNode(node);
                if (m_currentMainView == MainViewType::MixEditor)
                {
                }
                else
                {
                    m_dataViewComponent.refreshView(); // Refresh data view if it's the current view
                }
                m_mainPlaybackAndStatusPanel.setStatusMessage("Selected mixes deleted successfully.", false);
            }
            else
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("Mix deletion cancelled.", false);
            }
        }

        void MainComponent::deleteSelectedRows()
        {
            if (!m_currentSelectedDataNode)
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("No data node selected.", true);
                return;
            }
            if (m_currentMainView == MainViewType::MixEditor)
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("Cannot delete rows in Mix Editor view.", true);
                return;
            }
            const auto name{m_currentSelectedDataNode->getName()};
            const bool isWorkingSetsNode = (name == getWorkingSetsRootNodeName());
            auto selectedRows = m_dataViewComponent.getSelectedRowIndices();
            if (selectedRows.empty())
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("No rows selected for deletion.", true);
                return;
            }
            std::reverse(selectedRows.begin(),
                         selectedRows.end()); // Reverse to delete from end to start

            juce::String message;
            std::string okButtonText;
            if (selectedRows.size() == 1)
            {
                if (isWorkingSetsNode)
                {
                    message = "Are you sure you want to delete the selected "
                              "working-set";
                    okButtonText = "Delete Working Set";
                }
                else
                {
                    message = "Are you sure you want to delete the selected mix";
                    okButtonText = "Delete Mix";
                }
            }
            else
            {
                if (isWorkingSetsNode)
                {
                    message = std::format("Are you sure you want to delete the "
                                          "selected {} working-sets?",
                                          selectedRows.size());
                    okButtonText = "Delete Working Sets";
                }
                else
                {
                    message = std::format("Are you sure you want to delete the "
                                          "selected {} mixes?",
                                          selectedRows.size());
                    okButtonText = "Delete Mixes";
                }
            }
            const auto node = m_currentSelectedDataNode;

            juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon, // Icon type
                                               "Confirm Deletion",             // Window title
                                               message,
                                               okButtonText,                        // OK button text (can be "OK", "Delete", etc.)
                                               "Cancel",                            // Cancel button text
                                               nullptr,                             // Parent component (optional, nullptr for desktop)
                                               juce::ModalCallbackFunction::create( // Callback
                                                   [this, selectedRows,
                                                    node](int result) // Capture necessary data
                                                   {
                                                       onDeleteSelectedRows(selectedRows, node, result);
                                                   }));
        }

        bool MainComponent::createWorkingSet()
        {
            if (!m_currentSelectedDataNode)
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("No data node selected to create working set from.", true);
                return false;
            }
            if (m_currentMainView == MainViewType::MixEditor)
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("Cannot create working set in Mix Editor view.", true);
                return false;
            }

            if (m_dataViewComponent.getNumSelectedRows() > 0)
            {
                return createWorkingSetFromTrackIds(m_dataViewComponent.getSelectedTrackIds());
            }
            else if (m_currentSelectedDataNode)
            {
                return createWorkingSetFromNode(m_currentSelectedDataNode);
            }
            m_mainPlaybackAndStatusPanel.setStatusMessage("Internal error: no selection, and no current node?", true);
            return false;
        }

        bool MainComponent::createWorkingSetFromTrackIds(std::vector<TrackId> trackIds)
        {
            return onHandleCreateWorkingSetDialog(static_cast<int64_t>(trackIds.size()),
                                                  [this, trackIds](const juce::String &name)
                                                  {
                                                      onCreateWorkingSetFromTrackIdsCallback(name, trackIds);
                                                  });
        }

        void MainComponent::onCommonCreateWorkingSetCallback(bool success, const WorkingSetInfo &workingSetInfo)
        {
            if (success)
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("Working set '" + workingSetInfo.name + "' created successfully.", false);

                if (const auto workingSetsRootNode{m_rootNavigationNode->getWorkingSetsRootNode()})
                {
                    m_navigationPanel.refreshNode(workingSetsRootNode);
                    if (const auto wsNewNode{workingSetsRootNode->get(workingSetInfo.id)})
                    {
                        m_navigationPanel.selectNode(wsNewNode);
                        wsNewNode->release(REFCOUNT_DEBUG_ARGS);
                    }
                }
            }
            else
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("Failed to create working set: " + workingSetInfo.name, true);
            }
        }

        void MainComponent::onCreateWorkingSetFromTrackIdsCallback(const juce::String &name, std::vector<TrackId> trackIds)
        {
            WorkingSetInfo workingSetInfo;
            onCommonCreateWorkingSetCallback(theTrackLibrary.getWorkingSetManager().createWorkingSetFromTrackIds(trackIds, name.toStdString(), workingSetInfo),
                                             workingSetInfo);
        }

        bool MainComponent::createWorkingSetFromNode(const INavigationNode *node)
        {
            int64_t trackCount;
            if (!node->getNumberOfRows(trackCount))
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("Error retrieving track count from node.", true);
                return false;
            }
            return onHandleCreateWorkingSetDialog(trackCount,
                                                  [this, node](const juce::String &name)
                                                  {
                                                      onCreateWorkingSetFromNodeCallback(name, node);
                                                  });
        }

        bool MainComponent::onHandleCreateWorkingSetDialog(int64_t trackCount, OnCreateWorkingSetCallback callback)
        {
            if (trackCount <= 0)
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("No tracks available for working-set creation", true);
                return false;
            }
            auto *dialog = new CreateWorkingSetDialogComponent{trackCount, callback};

            juce::DialogWindow::LaunchOptions launchOptions;
            launchOptions.content.setOwned(dialog);
            launchOptions.dialogTitle = "Create Working Set";
            launchOptions.escapeKeyTriggersCloseButton = true;
            launchOptions.resizable = false;
            launchOptions.launchAsync();
            return true;
        }

        void MainComponent::onCreateWorkingSetFromNodeCallback(const juce::String &name, const INavigationNode *node)
        {
            assert(node != nullptr);
            WorkingSetInfo workingSetInfo;
            onCommonCreateWorkingSetCallback(
                theTrackLibrary.getWorkingSetManager().createWorkingSetFromQuery(*node->getQueryArgs(), name.toStdString(), workingSetInfo), workingSetInfo);
        }

        // helper method
        std::vector<TrackInfo> getAllTracks(const INavigationNode *node)
        {
            std::vector<TrackInfo> result;
            if (node)
            {
                int64_t nrRows = 0;
                if (node->getNumberOfRows(nrRows))
                {
                    for (int64_t index = 0; index < nrRows; ++index)
                    {
                        const auto pti{node->getTrackInfoForRow(static_cast<RowIndex_t>(index))};
                        if (pti)
                        {
                            result.push_back(*pti);
                        }
                    }
                }
            }
            return result;
        }

        void MainComponent::onDoRemoveMix(INavigationNode *selectedNode, const MixInfo &mixToDelete, int result)
        {
            if (result == 1) // User clicked "Delete Mix" (OK button)
            {
                spdlog::info("User confirmed deletion for mix ID: {} [{}]", mixToDelete.mixId, mixToDelete.name);
                const bool removed = theTrackLibrary.getMixManager().removeMix(mixToDelete.mixId);
                if (removed)
                {
                    m_mainPlaybackAndStatusPanel.setStatusMessage(std::format("Mix {} successfully removed.", mixToDelete.name), false);
                    // TODO: Refresh the NavigationPanel to remove the mix from
                    // the list This might involve telling NavigationPanel its
                    // root node or relevant part is dirty and it needs to
                    // rebuild its items. For example:
                    // m_navigationPanel.refreshMixes(); or
                    // m_navigationPanel.refreshNode(parentNodeOfMixes); Or if
                    // INavigationNode can notify its parent of changes:
                    //   parentNodeOfMixes->notifySubtreeChanged();
                    // Simplest might be to re-set the root node if your tree
                    // isn't too deep/complex to rebuild.
                    m_navigationPanel.removeNodeFromTree(selectedNode); // Assuming you implement such a method
                }
                else
                {
                    spdlog::error("Failed to remove mix ID: {} [{}]", mixToDelete.mixId, mixToDelete.name);
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Deletion Failed",
                                                           std::format("Could not remove the mix '{}' from the database.", mixToDelete.name));
                }
            }
            else // User clicked "Cancel" (result == 0) or closed the dialog
            {
                spdlog::info("User cancelled deletion for mix ID: {} [{}]", mixToDelete.mixId, mixToDelete.name);
                m_mainPlaybackAndStatusPanel.setStatusMessage("Mix deletion cancelled.", false);
            }
        }

        void MainComponent::onExportMix(INavigationNode *selectedNode)
        {
            assert(selectedNode != nullptr && "Selected node should not be null in onExportMix()");

            const auto mixNode{static_cast<MixNode *>(selectedNode)};
            const auto mixInfo{mixNode->getMixInfo()};

            const auto title{std::format("Export Mix '{}' As...", mixInfo.name)};
            // Store the FileChooser in a member unique_ptr to keep it alive
            m_activeFileChooser =
                std::make_unique<juce::FileChooser>(title, juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.mp3;*.wav", true, false, this);

            int chooserFlags =
                juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting;

            // Use std::bind (or a small capturing lambda) to call our member function
            // We need to pass mixId and mixName to the callback.
            m_activeFileChooser->launchAsync(chooserFlags,
                                             // This lambda is small and just forwards to the member function, passing captured state.
                                             // It is copyable.
                                             [this, mixInfo](const juce::FileChooser &chooser)
                                             {
                                                 this->onExportMixFileChooserModalDismissed(chooser, mixInfo);
                                             });
        }

        void MainComponent::onExportMixFileChooserModalDismissed(const juce::FileChooser &chooser, database::MixInfo mixInfo)
        {
            const juce::File chosenFile = chooser.getResult();

            // Release the FileChooser now that its job is done
            m_activeFileChooser.reset();

            if (chosenFile == juce::File{}) // User cancelled
            {
                return;
            }

            std::filesystem::path targetExportPath = chosenFile.getFullPathName().toStdString();
            spdlog::info("Exporting mix ID: {} (Name: '{}') to: {}", mixInfo.mixId, mixInfo.name, pathToString(targetExportPath));

            auto *task = new CreateMixTask(mixInfo, m_audioLibrary.getMixExporter(), targetExportPath);
            TaskDialog::launch("Mix Creation In Progress", task, 500, this);
            task->release(REFCOUNT_DEBUG_ARGS);
        }

        void MainComponent::onRemoveMix(INavigationNode *selectedNode)
        {
            assert(selectedNode != nullptr && "Selected node should not be null in onRemoveMix()");

            const auto mixNode{static_cast<MixNode *>(selectedNode)};
            const auto mixInfo{mixNode->getMixInfo()};

            juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon, // Icon type
                                               "Question",                     // Window title
                                               std::format("Are you sure you want to delete the mix {}?", mixInfo.name),
                                               "Delete Mix",                        // OK button text (can be "OK", "Delete", etc.)
                                               "Cancel",                            // Cancel button text
                                               nullptr,                             // Parent component (optional, nullptr for desktop)
                                               juce::ModalCallbackFunction::create( // Callback
                                                   [this, mixInfo,
                                                    mixNode](int result) // Capture necessary data
                                                   {
                                                       onDoRemoveMix(mixNode, mixInfo, result);
                                                   }));
        }

        void MainComponent::createMix()
        {
            if (!m_currentSelectedDataNode)
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("No data node selected.", true);
                return;
            }
            if (m_currentMainView == MainViewType::MixEditor)
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("Cannot create mix in Mix Editor view.", true);
                return;
            }
            std::vector<TrackInfo> selectedTracks{
                m_dataViewComponent.getSelectedTracks() // Assuming DataView has a method
                                                        // to get selected tracks
            };
            if (selectedTracks.size() <= 1)
            {
                // assume we want the full node - this is a heuristic only; at
                // some point we would want to get the information on the mode
                // directly from the UI. Good for now...
                selectedTracks = getAllTracks(m_currentSelectedDataNode);
            }

            // In MainComponent.cpp, inside createMix() method, after getting
            // selectedTracks :
            if (selectedTracks.empty())
            { // Or selectedTracks.size() < 2 for a meaningful mix
                m_mainPlaybackAndStatusPanel.setStatusMessage("Not enough tracks selected to create a mix.", true);
                return;
            }
            // Capture necessary data for the callback BEFORE selectedTracks is
            // moved. If the callback needs the original track IDs for cleanup,
            // get them now.
            std::vector<TrackId> trackIdsForCleanup;
            for (const auto &trackInfo : selectedTracks)
            {
                trackIdsForCleanup.push_back(trackInfo.trackId);
            }

            // The CreateMixDialogComponent will be managed by
            // DialogWindow::LaunchOptions
            auto *dialog = new ui::CreateMixDialogComponent(m_audioLibrary, selectedTracks,
                                                            [this](bool success, const MixInfo &mixInfo)
                                                            {
                                                                onMixCreatedCallback(success, mixInfo);
                                                            });

            juce::DialogWindow::LaunchOptions launchOptions;
            launchOptions.content.setOwned(dialog); // DialogWindow takes ownership
            launchOptions.dialogTitle = "Create Auto-Mix";
            launchOptions.escapeKeyTriggersCloseButton = true;
            launchOptions.resizable = false; // Or true if you prefer
            launchOptions.launchAsync();
        }

        void MainComponent::onMixCreatedCallback(bool success, const MixInfo &mixInfo)
        {
            if (success)
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("Mix '" + mixInfo.name + "' created successfully.", false);

                if (const auto mixesRootNode{m_rootNavigationNode->getMixesRootNode()})
                {
                    m_navigationPanel.refreshNode(mixesRootNode);
                    if (const auto newMixNode{mixesRootNode->get(mixInfo.mixId)})
                    {
                        m_navigationPanel.selectNode(newMixNode);
                        newMixNode->release(REFCOUNT_DEBUG_ARGS);
                    }
                }
            }
            else
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("Failed to create mix: " + mixInfo.name, true);
            }
        }

        void MainComponent::requestPlayOrPlaySelection()
        {
            // If a file is already loaded and paused, just play.
            if (m_playbackController.getCurrentState() == PlaybackController::State::Paused)
            {
                m_playbackController.play();
                return;
            }
            // If something is playing, this might toggle to pause, or do
            // nothing. PlaybackController::play() handles "play if loaded and
            // not playing". If nothing is loaded, try to play current
            // selection.

            if (m_playbackController.getCurrentFilepath().isEmpty() || m_playbackController.getCurrentState() == PlaybackController::State::Stopped)
            {

                m_playbackController.play(); // Will play if a track is loaded/paused.
            }
            else
            {
                m_playbackController.play(); // Play if loaded/paused, or resume.
            }
            syncPlaybackUIToControllerState();
        }

        bool MainComponent::onShowScanDialog()
        {
            auto *scanDialog = new ScanDialogComponent{};

            juce::DialogWindow::LaunchOptions launchOptions;
            launchOptions.content.setOwned(scanDialog);
            launchOptions.dialogTitle = "Manage Library Folders & Scan";
            launchOptions.escapeKeyTriggersCloseButton = true;
            launchOptions.resizable = true;
            scanDialog->onDialogClosed = [this]()
            {
                spdlog::info("MainComponent: ScanDialogComponent closed");
                if (m_currentMainView == MainViewType::MixEditor)
                {
                }
                else
                {
                    // If we are in DataView, we refresh it.
                    m_dataViewComponent.refreshView();
                }

                m_mainPlaybackAndStatusPanel.setStatusMessage("Scan dialog closed.", false);
            };

            launchOptions.launchAsync();
            m_mainPlaybackAndStatusPanel.setStatusMessage("Folder management dialog opened.", false);
            return true;
        }

        bool MainComponent::onShowAboutDialog()
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "About jucyaudio",
                                                   "jucyaudio - MP3 Player and Mixer\nVersion 0.1.0 "
                                                   "(Dev)\n\n(c) 2025 Your Name",
                                                   "OK");

            return true;
        }

        bool MainComponent::onShowConfigureColumnsDialog()
        {
            using namespace config;

            if(m_currentMainView == MainViewType::MixEditor)
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("Column configuration not available in Mix Editor view.", true);
                return false;
            }
            TypedValueVector<DataViewColumnSection> *pConfigSection = nullptr;
            const auto currentNode = m_dataViewComponent.getCurrentNode();
            if (!currentNode)
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("No node selected at all.", true);
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
                m_mainPlaybackAndStatusPanel.setStatusMessage("No valid node selected for column configuration.", true);
                return false;
            }

            const auto &availableCols = currentNode->getColumns();

            auto* columnDialog = new ColumnConfigurationDialogComponent(
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
);
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

        bool MainComponent::onApplyThemeByIndex(size_t themeIndex)
        {
            const auto &availableThemes = theThemeManager.getAvailableThemes();
            if (themeIndex < availableThemes.size())
            {
                const auto selectedThemeName = theThemeManager.applyTheme(m_lookAndFeel, themeIndex, this);
                m_navigationPanel.sendLookAndFeelChange();
                m_dataViewComponent.sendLookAndFeelChange();
                m_mixEditorComponent.sendLookAndFeelChange();
                m_playbackToolbar.sendLookAndFeelChange();
                m_mainPlaybackAndStatusPanel.sendLookAndFeelChange();
                m_dynamicToolbar.sendLookAndFeelChange();

                config::TomlBackend backend{g_strConfigFilename};
                config::theSettings.uiSettings.theme.set(selectedThemeName);
                config::theSettings.save(backend);
                return true;
            }
            spdlog::error("Invalid theme index: {}", themeIndex);
            return false;
        }

        bool MainComponent::onShowMaintenanceDialog()
        {
            class DatabaseMaintenanceTask final : public ILongRunningTask
            {
            public:
                DatabaseMaintenanceTask()
                    : ILongRunningTask{"Performing Database Maintenance", false}
                {
                }

                void run([[maybe_unused]] ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel) override
                {
                    theTrackLibrary.runMaintenanceTasks(shouldCancel);
                    completionCb(true, "Database maintenance completed successfully.");
                }
            };

            auto *task = new DatabaseMaintenanceTask{};
            TaskDialog::launch("Database Maintenance", task, {}, this);
            task->release(REFCOUNT_DEBUG_ARGS);
            return true;
        }

    } // namespace ui
} // namespace jucyaudio
