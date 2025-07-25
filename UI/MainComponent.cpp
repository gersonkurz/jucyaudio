#include <Config/toml_backend.h>
#include <Database/BackgroundService.h>
#include <Database/BackgroundTasks/BpmAnalysis.h>
#include <Database/BackgroundTasks/BpmAnalysisTask.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Nodes/MixNode.h>
#include <Database/Nodes/RootNode.h>
#include <Database/Nodes/TypedOverviewNode.h>
#include <Database/Nodes/VirtualFolderNode.h>
#include <Database/Nodes/WorkingSetNode.h>
#include <UI/ColumnConfiguratorDialog.h>
#include <UI/CreateMixDialogComponent.h>
#include <UI/CreateWorkingSetDialogComponent.h>
#include <UI/ILongRunningTask.h>
#include <UI/MainComponent.h>
#include <UI/MarkerEditDialog.h>
#include <UI/ScanDialogComponent.h>
#include <UI/TaskDialog.h>
#include <UI/WorkingSetMetaDataEditorDialog.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>
#include <algorithm>
#include <spdlog/spdlog.h>
#ifndef JUCE_WINDOWS
#include <unistd.h>
#endif

namespace jucyaudio
{
    using namespace database;

    namespace ui
    {
        MainViewType determineType(const INavigationNode *node)
        {
            const auto nodePath{getNodePath(node)};
            if (nodePath.size() >= 3)
            {
                if (nodePath[1]->getName() == getMixesRootNodeName())
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
              m_playbackController{m_hiddenPlaybackToolbar},
              m_enhancedPlayer{m_playbackController, m_audioFormatManager, m_audioThumbnailCache},
              m_mainPlaybackAndStatusPanel{*this}
        {
            theThemeManager.applyCurrentTheme(m_lookAndFeel, this);

            // Register audio formats
            m_audioFormatManager.registerBasicFormats();

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
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Engine Error",
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

            // Enhanced Player callbacks
            m_enhancedPlayer.onPreviousTrack = [this]
            {
                // TODO: Implement previous track logic
                spdlog::info("Previous track requested");
            };

            m_enhancedPlayer.onNextTrack = [this]
            {
                // TODO: Implement next track logic
                spdlog::info("Next track requested");
            };

            m_enhancedPlayer.onMarkerAction = [this](database::TrackId trackId, std::chrono::milliseconds position, bool isNewMarker)
            {
                spdlog::info("Marker action requested: track={}, position={}ms, isNew={}", trackId, position.count(), isNewMarker);

                showMarkerDialog(trackId, position, isNewMarker);
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
            menuManager.registerMenu("File",
                {// The lambda captures `this` from MainComponent, keeping logic and state together.
                    {"Scan Folders...",
                        "...",
                        [&]()
                        {
                            onShowScanDialog();
                        },
                        {{'s', juce::ModifierKeys::commandModifier}},
                        {}},
                    {"-"},
                    {"Database Maintenance...",
                        "...",
                        [&]()
                        {
                            onShowMaintenanceDialog();
                        }},
                    {"Build Virtual Folders...",
                        "...",
                        [&]()
                        {
                            onBuildVirtualFolders();
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

            menuManager.registerMenu("View",
                {{"Configure Columns...",
                    "...",
                    [&]()
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
            startTimerHz(30); // For smooth UI updates (e.g., playback position slider)

            // Initial size
            setSize(1200, 800);

            // Required for AudioAppComponent
            setAudioChannels(0, 2); // Output only

            // Setup the background service (assuming it's a member m_backgroundService)
            theBackgroundTaskService.start();

            // Create and register our new BPM analysis task.
            // Note: the service will retain() the task, so we can release our initial reference.
            auto *bpmTask = new background_tasks::BpmAnalysis{};
            theBackgroundTaskService.registerTask(bpmTask);
            bpmTask->release(REFCOUNT_DEBUG_ARGS);
        }

        MainComponent::~MainComponent()
        {
            theBackgroundTaskService.stop();
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
            // spdlog::info("MainComponent::paint - juce::ResizableWindow::backgroundColourId colour: '#{}'", backgroundColour.toString().toStdString());
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
                bottomPanelHeight = 120; // Increased for enhanced player

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

            // m_currentMainViewComponent->setBounds(dataViewX, centralArea.getY(), dataViewWidth, centralArea.getHeight());
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
            const auto start{std::chrono::high_resolution_clock::now()};
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

                if (currentViewType != newViewType)
                {
                    m_currentMainViewComponent->setVisible(false);
                    if (newViewType == MainViewType::MixEditor)
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

                    // Set up playback callback
                    m_mixEditorComponent.setPlaybackCallback(
                        [this](const juce::File &audioFile, double startPosition)
                        {
                            this->playFileFromPosition(audioFile, startPosition);
                        });

                    // Set up seek callback for live position changes
                    m_mixEditorComponent.setSeekCallback(
                        [this](double timePosition)
                        {
                            this->seekToTimelinePosition(timePosition);
                        });

                    // Set up track deletion callback
                    m_mixEditorComponent.setTrackDeletionCallback(
                        [this](TrackId trackId)
                        {
                            this->removeTrackFromMix(trackId);
                        });
                }
                else
                {
                    m_dataViewComponent.setCurrentNode(m_currentSelectedDataNode); // DataView updates its content source
                    m_dataViewComponent.refreshView();                             // Tell DataView to redraw
                }
                int64_t totalTracks = 0;
                if (m_currentSelectedDataNode->getTotalTrackCount(totalTracks))
                {
                    m_mainPlaybackAndStatusPanel.setStatusMessage(std::format("{} tracks in '{}'", totalTracks, m_currentSelectedDataNode->getName()), false);
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
                m_mainPlaybackAndStatusPanel.setStatusMessage("", false);
            }
            syncPlaybackUIToControllerState(); // Update play button enable
                                               // state
            const auto end{std::chrono::high_resolution_clock::now()};
            const auto duration{std::chrono::duration_cast<std::chrono::milliseconds>(end - start)};
            spdlog::info("MainComponent::handleNodeSelection took {} ms", duration.count());
        }

        void MainComponent::removeTrackFromMix(TrackId trackId)
        {
            if (!m_currentSelectedDataNode)
                return;

            MixId mixId = m_currentSelectedDataNode->getUniqueId();
            spdlog::info("Soft-deleting track {} from mix {}", trackId, mixId);

            auto *mixManager = &theTrackLibrary.getMixManager();
            if (mixManager->removeTrackFromMix(mixId, trackId))
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("Track removed from mix.", false);
                // Refresh the mix editor to show the change
                m_mixEditorComponent.loadMix(mixId);
            }
            else
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("Failed to remove track from mix.", true);
                spdlog::error("Failed to soft-delete track {} from mix {}", trackId, mixId);
            }
        }

        void MainComponent::seekToTimelinePosition(double timePosition)
        {
            spdlog::info("seekToTimelinePosition called with time: {:.2f}", timePosition);
            spdlog::info("Current playback state: {}", static_cast<int>(m_playbackController.getCurrentState()));

            if (m_playbackController.getCurrentState() == PlaybackController::State::Playing ||
                m_playbackController.getCurrentState() == PlaybackController::State::Paused)
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
                m_mainPlaybackAndStatusPanel.setStatusMessage("Position set to " + juce::String(timePosition, 1) + "s", false);
            }
        }

        void MainComponent::playFileFromPosition(const juce::File &audioFile, double startPosition)
        {
            if (audioFile.existsAsFile())
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage(
                    getSafeDisplayText("Playing: " + audioFile.getFileName() + " from " + juce::String(startPosition, 1) + "s"), false);

                if (!m_playbackController.loadAndPlayFileFromPosition(audioFile, startPosition))
                {
                    m_mainPlaybackAndStatusPanel.setStatusMessage(getSafeDisplayText("Error playing: " + audioFile.getFileName()), true);
                }
                else
                {
                    // Load waveform when playback starts successfully
                    m_enhancedPlayer.loadFile(audioFile);
                }
            }
            syncPlaybackUIToControllerState();
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
            case DataAction::EditMetadata:
                onEditMetadata(selectedNode);
                break;
            case DataAction::RunBpmAnalysis:
                onRunBpmAnalysis(selectedNode);
                break;
            case DataAction::RemoveWorkingSet:
                onRemoveWorkingSet(selectedNode);
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
            case DataAction::RunBpmAnalysis:
                onRunBpmAnalysisForSelectedRows();
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
                deleteSelectedRows(true);
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

            auto trackIds = node->getAllTrackIds();
            if (trackIds.empty())
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("No tracks to analyze.", true);
                return;
            }

            auto *task = new background_tasks::BpmAnalysisTask(std::move(trackIds));
            TaskDialog::launch("BPM Analysis",
                task,
                500,
                this,
                [this, task]()
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

        void MainComponent::onRunBpmAnalysisForSelectedRows()
        {
            auto trackIds = m_dataViewComponent.getSelectedTrackIds();
            if (trackIds.empty())
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("No tracks selected for analysis.", true);
                return;
            }

            auto *task = new background_tasks::BpmAnalysisTask(std::move(trackIds));
            TaskDialog::launch("BPM Analysis",
                task,
                500,
                this,
                [this, task]()
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
                message += juce::String(track.filepath.filename().string()) + juce::String("\n");
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
                            std::vector<database::TrackId> badTrackIds;
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
                                if (wsManager.removeFromWorkingSet(ws.id, badTrackIds))
                                {
                                    removedCount++;
                                }
                            }

                            m_mainPlaybackAndStatusPanel.setStatusMessage(
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
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, "Playback Error", "Cannot play file:\n" + audioFile.getFullPathName());

                    // Update track status to bad_format if it wasn't already marked
                    if (track->status != database::TrackStatus::BadFormat)
                    {
                        theTrackLibrary.getTrackDatabase()->updateTrackStatus(track->trackId, database::TrackStatus::BadFormat);
                    }
                }
                else
                {
                    // Update track status to ok if it wasn't already marked
                    if (track->status != database::TrackStatus::Ok)
                    {
                        theTrackLibrary.getTrackDatabase()->updateTrackStatus(track->trackId, database::TrackStatus::Ok);
                    }

                    // Load waveform when playback starts successfully
                    m_enhancedPlayer.loadFile(audioFile, track->trackId);

                    // Load markers for this track
                    const auto markers = theTrackLibrary.getMarkerManager().getMarkersForTrack(track->trackId);
                    m_enhancedPlayer.setMarkers(markers);
                }
            }
            else
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("Cannot play: " + std::to_string(track->trackId) + " (No path)", true);
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, "Playback Error", "Cannot find audio file for: " + std::to_string(track->trackId));
            }
            syncPlaybackUIToControllerState();
        }

        void MainComponent::onDeleteSelectedRows(DeleteContext *const dc, int result)
        {
            if (result == 1) // User clicked "Delete"
            {
                // what to do depends entirely on the context of the operation
                bool success = false;
                const auto nrSelectedRows{dc->selectedRows.size()};
                std::string statusMessage;
                switch (dc->node->getNodeType())
                {
                case NodeType::Mix:
                {
                    const auto mixNode{reinterpret_cast<MixNode *>(dc->node)};
                    const auto mixId{mixNode->getUniqueId()};
                    // if you call it from the list of tracks in a mix, you're actually
                    // removing tracks from the mix, not deleting the mix itself
                    if (dc->fromDataView)
                    {
                        const auto trackIds{m_dataViewComponent.getUnderlyingObjectIds<TrackId>(dc->selectedRows)};
                        success = theTrackLibrary.getMixManager().removeTracksFromMix(mixId, trackIds);
                        statusMessage = success ? "Removed tracks from mix." : "Failed to remove tracks from mix.";
                    }
                    else
                    {
                        // the navigation panel can select only one mix at a time
                        assert(nrSelectedRows <= 0);
                        success = theTrackLibrary.getMixManager().removeMix(mixId);
                        statusMessage = success ? "Mix deleted." : "Failed to delete mix.";
                    }
                    break;
                }
                case NodeType::MixesRoot:
                {
                    assert(dc->fromDataView); // You cannot select the root itself for deletion, must have come from the data view
                    const auto mixesRootNode{reinterpret_cast<TypedOverviewNode<MixInfo, MixNode> *>(dc->node)};
                    const auto mixIds{m_dataViewComponent.getUnderlyingObjectIds<MixId>(dc->selectedRows)};
                    success = theTrackLibrary.getMixManager().removeMixes(mixIds);
                    statusMessage = success ? "Mixes deleted." : "Failed to delete mixes.";
                    break;
                }
                case NodeType::WorkingSet:
                {
                    const auto workingSetNode{reinterpret_cast<WorkingSetNode *>(dc->node)};
                    const auto workingSetId{workingSetNode->getUniqueId()};
                    // if you call it from the list of tracks in a working-set, you're actually
                    // removing tracks from the working-set, not deleting the working-set itself
                    if (dc->fromDataView)
                    {
                        const auto trackIds{m_dataViewComponent.getUnderlyingObjectIds<TrackId>(dc->selectedRows)};
                        success = theTrackLibrary.getWorkingSetManager().removeFromWorkingSet(workingSetId, trackIds);
                        statusMessage = success ? "Removed tracks from working set." : "Failed to remove tracks from working set.";
                    }
                    else
                    {
                        // the navigation panel can select only one working-set at a time
                        assert(nrSelectedRows <= 0);
                        success = theTrackLibrary.getWorkingSetManager().removeWorkingSet(workingSetId);
                        statusMessage = success ? "Working set deleted." : "Failed to delete working set.";
                    }
                    break;
                }
                case NodeType::WorkingSetsRoot:
                {
                    assert(dc->fromDataView); // You cannot select the root itself for deletion, must have come from the data view
                    const auto mixesRootNode{reinterpret_cast<TypedOverviewNode<WorkingSetInfo, WorkingSetNode> *>(dc->node)};
                    const auto workingSetIds{m_dataViewComponent.getUnderlyingObjectIds<WorkingSetId>(dc->selectedRows)};
                    success = theTrackLibrary.getWorkingSetManager().removeWorkingSets(workingSetIds);
                    statusMessage = success ? "Working sets deleted." : "Failed to delete working sets.";
                    break;
                }
                default:
                    assert(false);
                    return;
                }
                if (success)
                {
                    for (const auto rowIndex : dc->selectedRows)
                    {
                        spdlog::info("Deleting object at row {}", rowIndex);
                        dc->node->removeObjectAtRow(rowIndex);
                    }
                    m_navigationPanel.refreshNode(dc->node);

                    if (m_currentMainView != MainViewType::MixEditor)
                    {
                        if (dc->fromDataView)
                        {
                            m_dataViewComponent.refreshView(); // Refresh data view if it's the current view
                        }
                        else
                        {
                            // we may have to delete node from navigation and also select another one. Could be - not sure about that yet.
                        }
                    }
                }
                m_mainPlaybackAndStatusPanel.setStatusMessage(statusMessage, false);
                dc->node->release(REFCOUNT_DEBUG_ARGS);
                delete dc;
            }
            else
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("Operation cancelled", false);
            }
        }

        void MainComponent::deleteSelectedRows(bool fromDataView)
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
            const auto nodePath{getNodePath(m_currentSelectedDataNode)};
            if (nodePath.size() == 1)
            {
                // we're being called from the root node, so we're about to delete complete mixes / working sets
            }
            const auto dc{new DeleteContext{}};
            dc->fromDataView = fromDataView; // Store if this was called from DataView or NavigationPanel
            if (fromDataView)
            {
                // If called from DataView, get selected rows from DataView
                dc->selectedRows = m_dataViewComponent.getSelectedRowIndices();
                if (dc->selectedRows.empty())
                {
                    m_mainPlaybackAndStatusPanel.setStatusMessage("No rows selected for deletion.", true);
                    return;
                }
                // Reverse to delete from end to start
                std::reverse(dc->selectedRows.begin(), dc->selectedRows.end());
            }
            std::string warningMessage;
            std::string okButtonText;
            const auto nrSelectedRows{dc->selectedRows.size()};
            dc->node = m_currentSelectedDataNode;
            dc->node->retain(REFCOUNT_DEBUG_ARGS); // Retain the node to ensure it stays valid during deletion
            const auto nodeName{dc->node->getName()};
            switch (dc->node->getNodeType())
            {
            case NodeType::Mix:
                // if you call it from the list of tracks in a mix, you're actually
                // removing tracks from the mix, not deleting the mix itself
                if (fromDataView)
                {
                    if (nrSelectedRows == 1)
                    {
                        const auto assumedTrackName{dc->node->getCellText(dc->selectedRows[0], 0)};
                        warningMessage = std::format("Do you want to remove the track {} from the mix {}?", assumedTrackName, nodeName); // TODO: retrieve mix name
                        okButtonText = "Remove Track";
                    }
                    else
                    {
                        warningMessage = std::format("Do you want to remove the {} selected tracks from the mix {}?", nrSelectedRows, nodeName);
                        okButtonText = "Remove Tracks";
                    }
                }
                else
                {
                    // the navigation panel can select only one mix at a time
                    assert(nrSelectedRows <= 0);
                    warningMessage = std::format("Do you want to delete the mix {}?", nodeName); // TODO: retrieve mix name
                    okButtonText = "Delete Mix";
                }
                break;
            case NodeType::MixesRoot:
                // if you call it from the list of mixes in the root node
                if (fromDataView)
                {
                    // so this call must originate from the data-view
                    warningMessage = std::format("Do you want to delete the {} selected mixes?", nrSelectedRows);
                    okButtonText = "Delete Mixes";
                }
                else
                {
                    spdlog::error("logic problem: you cannot remove the root node itself, so this is should never happen");
                    return;
                }
                break;
            case NodeType::WorkingSet:
                // if you call it from the list of tracks in a working-set, you're actually
                // removing tracks from the working-set, not deleting the working-set itself
                if (fromDataView)
                {
                    if (nrSelectedRows == 1)
                    {
                        const auto assumedTrackName{dc->node->getCellText(dc->selectedRows[0], 0)};
                        warningMessage = std::format("Do you want to remove {} from the working-set {}?", assumedTrackName, nodeName);
                        okButtonText = "Remove Track";
                    }
                    else
                    {
                        warningMessage = std::format("Do you want to remove the {} selected tracks from the working-set {}?", nrSelectedRows, nodeName);
                        okButtonText = "Remove Tracks";
                    }
                }
                else
                {
                    // the navigation panel can select only one working-set at a time
                    assert(nrSelectedRows <= 0);
                    warningMessage = std::format("Do you want to delete the working-set {}?", nodeName); // TODO: retrieve working-set name
                    okButtonText = "Delete Working-Set";
                }
                break;
            case NodeType::WorkingSetsRoot:
                // if you call it from the list of working-sets in the root node
                if (fromDataView)
                {
                    // so this call must originate from the data-view
                    warningMessage = std::format("Do you want to delete the {} selected working-sets?", nrSelectedRows);
                    okButtonText = "Delete Working-Sets";
                }
                else
                {
                    spdlog::error("logic problem: you cannot remove the root node itself, so this is should never happen");
                    return;
                }
                break;
            default:
                spdlog::error("Unsupported node type for deletion: {}", static_cast<int>(m_currentSelectedDataNode->getNodeType()));
                return;
            }

            const auto node = m_currentSelectedDataNode;

            juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon, // Icon type
                "Confirm Deletion",                                            // Window title
                warningMessage,
                okButtonText, // OK button text (can be "OK", "Delete", etc.)
                "Cancel",     // Cancel button text
                nullptr,      // Parent component (optional, nullptr for desktop)
                juce::ModalCallbackFunction::create(
                    [this, dc](int result)
                    {
                        onDeleteSelectedRows(dc, result);
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
            onCommonCreateWorkingSetCallback(
                theTrackLibrary.getWorkingSetManager().createWorkingSetFromTrackIds(trackIds, name.toStdString(), workingSetInfo), workingSetInfo);
        }

        bool MainComponent::createWorkingSetFromNode(const INavigationNode *node)
        {
            int64_t trackCount;
            if (!node->getTotalTrackCount(trackCount))
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

        void MainComponent::onExportMix(INavigationNode *selectedNode)
        {
            assert(selectedNode != nullptr && "Selected node should not be null in onExportMix()");

            const auto mixNode{static_cast<MixNode *>(selectedNode)};
            const auto mixInfo{mixNode->getMixInfo()};

            const auto title{std::format("Export Mix '{}' As...", mixInfo.name)};
            m_activeFileChooser = std::make_unique<juce::FileChooser>(
                title, juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.mp3;*.wav;*.m3u", true, false, this);

            int chooserFlags =
                juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting;

            m_activeFileChooser->launchAsync(chooserFlags,
                [this, mixInfo](const juce::FileChooser &chooser)
                {
                    this->onExportMixFileChooserModalDismissed(chooser, mixInfo);
                });
        }

        class FinalizeAndExportTask : public ILongRunningTask
        {
        public:
            FinalizeAndExportTask(const MixInfo &mixInfo, const audio::IMixExporter &exporter, const std::filesystem::path &outputPath)
                : ILongRunningTask("Finalizing and Exporting Mix", true),
                  m_mixInfo(mixInfo),
                  m_exporter(exporter),
                  m_outputPath(outputPath)
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
                /*
                auto activeTracks = theTrackLibrary.getMixManager().getMixTracks(m_mixInfo.mixId);
                if (activeTracks.empty())
                {
                    completionCb(false, "No active tracks in the mix to export.");
                    return;
                }
                */
                if (m_exporter.exportMixToFile(m_mixInfo.mixId,
                        m_outputPath,
                        [&](float progress, const std::string &message)
                        {
                            progressCb(static_cast<int>(progress * 100.0f), message);
                            return !shouldCancel.load();
                        }))
                {
                    completionCb(true, "Mix successfully finalized and exported.");
                }
                else
                {
                    completionCb(false, "Unable to export mix to file: " + m_outputPath.string());
                }
            }

        private:
            MixInfo m_mixInfo;
            const audio::IMixExporter &m_exporter;
            const std::filesystem::path m_outputPath;
        };

        void MainComponent::onExportMixFileChooserModalDismissed(const juce::FileChooser &chooser, MixInfo mixInfo)
        {
            const juce::File chosenFile = chooser.getResult();
            m_activeFileChooser.reset();

            if (chosenFile == juce::File{}) // User cancelled
            {
                return;
            }

            std::filesystem::path targetExportPath = chosenFile.getFullPathName().toStdString();
            spdlog::info("Finalizing and exporting mix ID: {} (Name: '{}') to: {}", mixInfo.mixId, mixInfo.name, pathToString(targetExportPath));

            //(const MixInfo &mixInfo, audio::MixExporter &exporter, const std::filesystem::path &outputPath)
            auto *task = new FinalizeAndExportTask{mixInfo, m_audioLibrary.getMixExporter(), targetExportPath};
            TaskDialog::launch("Finalize & Export", task, 500, this);
            task->release(REFCOUNT_DEBUG_ARGS);
        }

        void MainComponent::onEditMetadata(INavigationNode *selectedNode)
        {
            if (auto *wsNode = dynamic_cast<WorkingSetNode *>(selectedNode))
            {
                const auto wsInfo = wsNode->getWorkingSetInfo();

                auto *dialog = new WorkingSetMetaDataEditorDialog{wsInfo,
                    [this, wsNode](bool nameChanged, std::string_view newName)
                    {
                        if (nameChanged)
                        {
                            spdlog::info("Working set name changed to: {}", newName);
                            wsNode->rename(newName);
                            if (auto navTreeItem = m_navigationPanel.findTreeViewItemForNode(wsNode))
                            {
                                navTreeItem->getOwnerView()->repaint();
                            }
                        }
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
                m_mainPlaybackAndStatusPanel.setStatusMessage("Details not available for this item.", true);
            }
        }

        void MainComponent::onRemoveWorkingSet(INavigationNode *selectedNode)
        {
            assert(selectedNode != nullptr && "Selected node should not be null in onRemoveWorkingSet()");

            const auto workingSetNode{static_cast<WorkingSetNode *>(selectedNode)};
            const auto workingSetInfo{workingSetNode->getWorkingSetInfo()};

            juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon, // Icon type
                "Question",                                                    // Window title
                std::format("Are you sure you want to delete the working-set {}?", workingSetInfo.name),
                "Delete Mix",                        // OK button text (can be "OK", "Delete", etc.)
                "Cancel",                            // Cancel button text
                nullptr,                             // Parent component (optional, nullptr for desktop)
                juce::ModalCallbackFunction::create( // Callback
                    [this,
                        workingSetInfo,
                        workingSetNode](int result) // Capture necessary data
                    {
                        onDoRemoveWorkingSet(workingSetNode, workingSetInfo, result);
                    }));
        }

        void MainComponent::onDoRemoveWorkingSet(INavigationNode *selectedNode, const WorkingSetInfo &workingSetToDelete, int result)
        {
            if (result == 1)
            {
                spdlog::info("User confirmed deletion for working-set ID: {} [{}]", workingSetToDelete.id, workingSetToDelete.name);
                const bool removed = theTrackLibrary.getWorkingSetManager().removeWorkingSet(workingSetToDelete.id);
                if (removed)
                {
                    m_mainPlaybackAndStatusPanel.setStatusMessage(std::format("Working-Set {} successfully removed.", workingSetToDelete.name), false);
                    m_navigationPanel.removeNodeFromTree(selectedNode); // Assuming you implement such a method
                }
                else
                {
                    spdlog::error("Failed to remove working-set ID: {} [{}]", workingSetToDelete.id, workingSetToDelete.name);
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                        "Deletion Failed",
                        std::format("Could not remove the working-set '{}' from the database.", workingSetToDelete.name));
                }
            }
            else // User clicked "Cancel" (result == 0) or closed the dialog
            {
                spdlog::info("User cancelled deletion for working-set ID: {} [{}]", workingSetToDelete.id, workingSetToDelete.name);
                m_mainPlaybackAndStatusPanel.setStatusMessage("Working-set deletion cancelled.", false);
            }
        }

        void MainComponent::onRemoveMix(INavigationNode *selectedNode)
        {
            assert(selectedNode != nullptr && "Selected node should not be null in onRemoveMix()");

            const auto mixNode{static_cast<MixNode *>(selectedNode)};
            const auto mixInfo{mixNode->getMixInfo()};

            juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon, // Icon type
                "Question",                                                    // Window title
                std::format("Are you sure you want to delete the mix {}?", mixInfo.name),
                "Delete Mix",                        // OK button text (can be "OK", "Delete", etc.)
                "Cancel",                            // Cancel button text
                nullptr,                             // Parent component (optional, nullptr for desktop)
                juce::ModalCallbackFunction::create( // Callback
                    [this,
                        mixInfo,
                        mixNode](int result) // Capture necessary data
                    {
                        onDoRemoveMix(mixNode, mixInfo, result);
                    }));
        }

        void MainComponent::onDoRemoveMix(INavigationNode *selectedNode, const MixInfo &mixToDelete, int result)
        {
            if (result == 1)
            {
                spdlog::info("User confirmed deletion for mix ID: {} [{}]", mixToDelete.mixId, mixToDelete.name);
                const bool removed = theTrackLibrary.getMixManager().removeMix(mixToDelete.mixId);
                if (removed)
                {
                    m_mainPlaybackAndStatusPanel.setStatusMessage(std::format("Mix {} successfully removed.", mixToDelete.name), false);
                    m_navigationPanel.removeNodeFromTree(selectedNode); // Assuming you implement such a method
                }
                else
                {
                    spdlog::error("Failed to remove mix ID: {} [{}]", mixToDelete.mixId, mixToDelete.name);
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                        "Deletion Failed",
                        std::format("Could not remove the mix '{}' from the database.", mixToDelete.name));
                }
            }
            else // User clicked "Cancel" (result == 0) or closed the dialog
            {
                spdlog::info("User cancelled deletion for mix ID: {} [{}]", mixToDelete.mixId, mixToDelete.name);
                m_mainPlaybackAndStatusPanel.setStatusMessage("Mix deletion cancelled.", false);
            }
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

            // Capture the source working set ID from the current node
            const WorkingSetId source_ws_id = m_currentSelectedDataNode->getUniqueId();

            std::vector<TrackInfo> selectedTracks{m_dataViewComponent.getSelectedTracks()};
            if (selectedTracks.size() <= 1)
            {
                selectedTracks = getAllTracks(m_currentSelectedDataNode);
            }

            if (selectedTracks.empty())
            {
                m_mainPlaybackAndStatusPanel.setStatusMessage("Not enough tracks selected to create a mix.", true);
                return;
            }

            auto *dialog = new ui::CreateMixDialogComponent(m_audioLibrary,
                selectedTracks,
                source_ws_id,
                [this](bool success, const MixInfo &mixInfo)
                {
                    onMixCreatedCallback(success, mixInfo);
                });

            juce::DialogWindow::LaunchOptions launchOptions;
            launchOptions.content.setOwned(dialog);
            launchOptions.dialogTitle = "Create Auto-Mix";
            launchOptions.escapeKeyTriggersCloseButton = true;
            launchOptions.resizable = false;
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
            if (m_playbackController.getCurrentState() == PlaybackController::State::Paused)
            {
                m_playbackController.play();
                return;
            }

            // No selection and nothing to resume
            m_mainPlaybackAndStatusPanel.setStatusMessage("No track selected to play.", true);
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

                // Force the Folders node to refresh its cache to pick up new folders
                if (const auto foldersRootNode = m_rootNavigationNode->getFoldersRootNode())
                {
                    foldersRootNode->refreshCache(true); // true = flush cache

                    // Find the tree item and trigger a visual update
                    if (auto *treeItem = m_navigationPanel.findTreeViewItemForNode(foldersRootNode))
                    {
                        treeItem->treeHasChanged();
                    }

                    foldersRootNode->release(REFCOUNT_DEBUG_ARGS);
                }

                // If we are in DataView, we refresh it.
                if (m_currentMainView == MainViewType::DataView)
                {
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
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                "About jucyaudio",
                "jucyaudio - MP3 Player and Mixer\nVersion 0.1.0 "
                "(Dev)\n\n(c) 2025 Your Name",
                "OK");

            return true;
        }

        bool MainComponent::onShowConfigureColumnsDialog()
        {
            using namespace config;

            if (m_currentMainView == MainViewType::MixEditor)
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
                m_enhancedPlayer.sendLookAndFeelChange();
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

        bool MainComponent::onBuildVirtualFolders()
        {
            class BuildVirtualFoldersTask final : public ILongRunningTask
            {
            public:
                BuildVirtualFoldersTask()
                    : ILongRunningTask{"Building Virtual Folders", false}
                {
                }

                void run(ProgressCallback progressCb, CompletionCallback completionCb, [[maybe_unused]] std::atomic<bool> &shouldCancel) override
                {
                    // Get the track database
                    auto *trackDb{theTrackLibrary.getTrackDatabase()};
                    if (!trackDb)
                    {
                        completionCb(false, "Failed to access track database.");
                        return;
                    }

                    // Build virtual folders with progress callback
                    auto result{trackDb->buildVirtualFolders(
                        [progressCb](float progress, const std::string &status)
                        {
                            if (progressCb)
                            {
                                progressCb(progress, status);
                            }
                        })};

                    if (result.isOk())
                    {
                        completionCb(true, "Virtual folders built successfully.");
                    }
                    else
                    {
                        completionCb(false, std::format("Failed to build virtual folders: {}", result.errorMessage));
                    }
                }
            };

            // Show confirmation dialog
            const int result{juce::AlertWindow::showYesNoCancelBox(juce::AlertWindow::QuestionIcon,
                "Build Virtual Folders",
                "This will analyze all tracks in your library and build virtual folders for fast navigation.\n\n"
                "This is a one-time operation that may take a few minutes for large libraries.\n\n"
                "Do you want to continue?",
                "Yes",
                "No",
                "",
                this)};

            if (result == 1) // Yes
            {
                auto *task = new BuildVirtualFoldersTask{};
                TaskDialog::launch("Building Virtual Folders", task, {}, this);
                task->release(REFCOUNT_DEBUG_ARGS);
                return true;
            }

            return false;
        }

        void MainComponent::showMarkerDialog(database::TrackId trackId, std::chrono::milliseconds position, bool isNewMarker)
        {
            auto &markerManager = theTrackLibrary.getMarkerManager();

            // markerManager is a reference, so it's always valid

            auto *dialog = new MarkerEditDialog();

            if (isNewMarker)
            {
                // Set up for new marker
                dialog->setupForNewMarker(position);

                dialog->onSave = [this, dialog, &markerManager, trackId, position]()
                {
                    const auto comment = dialog->getComment().toStdString();
                    if (comment.empty())
                    {
                        m_mainPlaybackAndStatusPanel.setStatusMessage("Marker comment cannot be empty", true);
                        return;
                    }

                    MarkerId newMarkerId;
                    const auto result = markerManager.createMarker(trackId, position, comment, newMarkerId);

                    if (result == MarkerResult::Success)
                    {
                        spdlog::info("Created marker {} for track {} at {}ms", newMarkerId, trackId, position.count());
                        m_mainPlaybackAndStatusPanel.setStatusMessage("Marker created", false);

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
                        m_mainPlaybackAndStatusPanel.setStatusMessage("Failed to create marker", true);
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
                        m_mainPlaybackAndStatusPanel.setStatusMessage("Marker comment cannot be empty", true);
                        return;
                    }

                    const auto result = markerManager.updateMarker(marker.markerId, newComment);

                    if (result == MarkerResult::Success)
                    {
                        spdlog::info("Updated marker {}", marker.markerId);
                        m_mainPlaybackAndStatusPanel.setStatusMessage("Marker updated", false);

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
                        m_mainPlaybackAndStatusPanel.setStatusMessage("Failed to update marker", true);
                    }
                };

                // Delete callback
                dialog->onDelete = [this, dialog, &markerManager, marker, trackId]()
                {
                    const auto result = markerManager.deleteMarker(marker.markerId);

                    if (result == MarkerResult::Success)
                    {
                        spdlog::info("Deleted marker {}", marker.markerId);
                        m_mainPlaybackAndStatusPanel.setStatusMessage("Marker deleted", false);

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
                        m_mainPlaybackAndStatusPanel.setStatusMessage("Failed to delete marker", true);
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

    } // namespace ui
} // namespace jucyaudio
