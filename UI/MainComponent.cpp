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
#include <UI/EditWorkingSetMetaDataDialog.h>
#include <UI/EditMixMetaDataDialog.h>
#include <UI/ILongRunningTask.h>
#include <UI/MainComponent.h>
#include <UI/MarkerEditDialog.h>
#include <UI/ScanDialogComponent.h>
#include <UI/TaskDialog.h>
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

    namespace // anonymous
    {
        // @brief This is the actual last-known view type, used internally
        ui::MainViewType lastKnownViewType{ui::MainViewType::DataView};

        // @brief This is the last-known view type FOR MIXES ONLY
        ui::MainViewType lastKnownViewTypeForMixes{ui::MainViewType::MixEditor};
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
            // Add more types as needed
            lastKnownViewType = MainViewType::DataView; // Default to DataView if no specific type matches
            return lastKnownViewType;
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
              m_statusPanel{*this},
              m_navigationTree{m_navigationPanel, m_dataViewComponent}
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

            m_enhancedPlayer.onMarkerAction = [this](TrackId trackId, std::chrono::milliseconds position, bool isNewMarker)
            {
                spdlog::info("Marker action requested: track={}, position={}ms, isNew={}", trackId, position.count(), isNewMarker);

                showMarkerDialog(trackId, position, isNewMarker);
            };

            if (!m_navigationTree.initialize())
            {
                // TODO: have a way to crash here?
                m_statusPanel.setStatusMessage("Error: Could not load navigation.", true);
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
            if (m_currentNode)
            {
                m_currentNode->dataNoLongerShowing(); // Good practice
                m_currentNode->release(REFCOUNT_DEBUG_ARGS);
                m_currentNode = nullptr;
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
            auto bounds = getLocalBounds();
            // Get actual heights after components are potentially laid out once
            int toolbarHeight = m_dynamicToolbar.isVisible() ? m_dynamicToolbar.getHeight() : 0;
            if (toolbarHeight == 0 && m_dynamicToolbar.isVisible())
                toolbarHeight = 40; // Default if not yet sized

            int bottomPanelHeight = m_statusPanel.isVisible() ? m_statusPanel.getHeight() : 0;
            if (bottomPanelHeight == 0 && m_statusPanel.isVisible())
                bottomPanelHeight = 120; // Increased for enhanced player

            m_dynamicToolbar.setBounds(bounds.removeFromTop(toolbarHeight));
            m_statusPanel.setBounds(bounds.removeFromBottom(bottomPanelHeight));

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
            m_statusPanel.setStatusMessage("Audio device prepared.", false);
        }

        void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill)
        {
            m_playbackController.getNextAudioBlock(bufferToFill);
        }

        void MainComponent::releaseResources()
        {
            m_playbackController.releaseResources();
            m_statusPanel.setStatusMessage("Audio resources released.", false);
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
            // m_currentNode or if DataView has selection. This
            // boolean was used by PlaybackController to enable/disable play
            // button if nothing is cued.
            bool canPlaySelection = (m_currentNode != nullptr); // Simplistic: if a node is selected.
                                                                // More accurately: if data view has a selected row
                                                                // and that row represents a playable track.
            m_playbackController.syncUIToPlaybackControllerState(canPlaySelection);
        }

        // --- Handler Method Stubs / Basic Logic ---
        void MainComponent::handleNodeSelection(INavigationNode *selectedNode, bool forceDisplaySwitch) // selectedNode is retained by caller (NavPanel)
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
            }

            if (m_currentNode)
            {
                const auto newViewType{determineMainViewType(m_currentNode)};
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
                const auto path{getNodePath(m_currentNode)};

                m_currentNode->prepareToShowData();
                m_dynamicToolbar.setCurrentNode(m_currentNode); // Toolbar updates its actions
                if (m_currentMainView == MainViewType::MixEditor)
                {
                    m_mixEditorComponent.loadMix(m_currentNode->getUniqueId()); // Load the mix data

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
                    m_dataViewComponent.setCurrentNode(m_currentNode); // DataView updates its content source
                    m_dataViewComponent.refreshView();                 // Tell DataView to redraw
                }
                int64_t totalTracks = 0;
                if (m_currentNode->getTotalTrackCount(totalTracks))
                {
                    m_statusPanel.setStatusMessage(std::format("{} tracks in '{}'", totalTracks, m_currentNode->getName()), false);
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
                m_statusPanel.setStatusMessage("", false);
            }
            syncPlaybackUIToControllerState(); // Update play button enable
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
                m_statusPanel.setStatusMessage("Track removed from mix.", false);
                // Refresh the mix editor to show the change
                m_mixEditorComponent.loadMix(mixId);
            }
            else
            {
                m_statusPanel.setStatusMessage("Failed to remove track from mix.", true);
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
                m_statusPanel.setStatusMessage("Position set to " + juce::String(timePosition, 1) + "s", false);
            }
        }

        void MainComponent::playFileFromPosition(const juce::File &audioFile, double startPosition)
        {
            if (audioFile.existsAsFile())
            {
                m_statusPanel.setStatusMessage(
                    getSafeDisplayText("Playing: " + audioFile.getFileName() + " from " + juce::String(startPosition, 1) + "s"), false);

                if (!m_playbackController.loadAndPlayFileFromPosition(audioFile, startPosition))
                {
                    m_statusPanel.setStatusMessage(getSafeDisplayText("Error playing: " + audioFile.getFileName()), true);
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
            if (m_currentNode)
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

                if (m_currentNode->setSearchTerms(searchTerms))
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
            if (!m_currentNode)
                return;
            handleNodeActionFromNavigationPanel(m_currentNode, action);
        }

        void MainComponent::handleNodeActionFromNavigationPanel(INavigationNode *node, DataAction action)
        {
            m_statusPanel.setStatusMessage("Node action: " + juce::String(dataActionToString(action, m_currentNode)), false);

            switch (action)
            {
            case DataAction::CreateWorkingSet:
                createWorkingSet();
                break;
            case DataAction::CreateMix:
                createMix();
                break;
            case DataAction::Delete:
                onDataActionDeleteSelectedObjects();
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
                onDataActionDeleteSelectedObjects();
                break;
            case DataAction::RunBpmAnalysis:
                onRunBpmAnalysisForSelectedRows();
                break;
            case DataAction::ShowDetails:
                m_statusPanel.setStatusMessage("Show details for: " + std::to_string(rowIndex), false);
                break;
            case DataAction::RemoveTracks: // TODO: we should do this only from the data View
                onDataActionRemoveTracks();
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
                m_statusPanel.setStatusMessage("No tracks to analyze.", true);
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
            auto objectIds = m_dataViewComponent.getSelectedObjectIds();
            if (objectIds.empty())
            {
                m_statusPanel.setStatusMessage("No tracks selected for analysis.", true);
                return;
            }

            auto *task = new background_tasks::BpmAnalysisTask(std::move(objectIds));
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
                                if (wsManager.removeFromWorkingSet(ws.id, badTrackIds))
                                {
                                    removedCount++;
                                }
                            }

                            m_statusPanel.setStatusMessage(
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
                m_statusPanel.setStatusMessage("No node selected for playback.", true);
                return;
            }
            const auto track{m_currentNode->getTrackInfoForRow(rowIndex)};
            if (!track)
            {
                m_statusPanel.setStatusMessage("No track info available for row: " + std::to_string(rowIndex), true);
                return;
            }

            juce::File audioFile{jucePathFromFs(track->filepath)};
            if (audioFile.existsAsFile())
            {
                // uncomment this line, and you get the exceptio
                m_statusPanel.setStatusMessage(getSafeDisplayText("Playing: " + audioFile.getFileName()), false);
                if (!m_playbackController.loadAndPlayFile(audioFile))
                {
                    m_statusPanel.setStatusMessage(getSafeDisplayText("Error playing: " + audioFile.getFileName()), true);
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
                m_statusPanel.setStatusMessage("Cannot play: " + std::to_string(track->trackId) + " (No path)", true);
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, "Playback Error", "Cannot find audio file for: " + std::to_string(track->trackId));
            }
            syncPlaybackUIToControllerState();
        }

        void MainComponent::onRemoveRowsFromCurrentNode(DeleteContext *const dc, int result)
        {
            if (result == 1) // User clicked "Delete"
            {
                std::string statusMessage;
                if (m_navigationTree.removeObjectsForRows(dc->node, dc->selectedRows))
                {
                    statusMessage = std::format("Removed tracks from {} {}", dc->node->m_refTypeNameForSingleObject, dc->node->getName());
                }
                else
                {
                    statusMessage = std::format("Failed to remove tracks from {} {}", dc->node->m_refTypeNameForSingleObject, dc->node->getName());
                }
                m_statusPanel.setStatusMessage(statusMessage, false);
                dc->node->release(REFCOUNT_DEBUG_ARGS);
                delete dc;
            }
            else
            {
                m_statusPanel.setStatusMessage("Operation cancelled", false);
            }
        }

        void MainComponent::onDataActionDeleteSelectedObjects()
        {
            // TODO: determine object type better
            onDataActionRemoveNamedObjects("object", "objects");
        }

        void MainComponent::onDataActionRemoveTracks()
        {
            onDataActionRemoveNamedObjects("track", "tracks");
        }

        void MainComponent::onDataActionRemoveNamedObjects(std::string_view itemTypeSingular, std::string_view itemTypePlural)
        {
            if (!m_currentNode)
            {
                m_statusPanel.setStatusMessage("No data node selected.", true);
                return;
            }
            if (m_currentMainView == MainViewType::MixEditor)
            {
                m_statusPanel.setStatusMessage("Cannot delete rows in Mix Editor view.", true);
                return;
            }
            const auto dc{new DeleteContext{}};
            dc->selectedRows = m_dataViewComponent.getSelectedRowIndices();
            if (dc->selectedRows.empty())
            {
                m_statusPanel.setStatusMessage("No rows selected for deletion.", true);
                return;
            }
            std::reverse(dc->selectedRows.begin(), dc->selectedRows.end());
            std::string warningMessage;
            std::string okButtonText;
            const auto nrSelectedRows{dc->selectedRows.size()};
            dc->node = m_currentNode;
            dc->node->retain(REFCOUNT_DEBUG_ARGS); // Retain the node to ensure it stays valid during deletion
            const auto nodeName{dc->node->getName()};

            if (nrSelectedRows == 1)
            {
                const auto assumedTrackName{dc->node->getCellText(dc->selectedRows[0], 0)};
                warningMessage = std::format(
                    "Do you want to remove the {} {} from the {} {}?", itemTypeSingular, assumedTrackName, dc->node->m_refTypeNameForSingleObject, nodeName);
                okButtonText = std::format("Remove {}", itemTypeSingular);
            }
            else
            {
                warningMessage = std::format(
                    "Do you want to remove the {} {} from the {} {}?", nrSelectedRows, itemTypePlural, dc->node->m_refTypeNameForSingleObject, nodeName);
                okButtonText = std::format("Remove {}", itemTypePlural);
            }

            juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon, // Icon type
                "Confirm Deletion",                                            // Window title
                warningMessage,
                okButtonText,
                "Cancel",
                nullptr, // TODO: use this window as parent instead
                juce::ModalCallbackFunction::create(
                    [this, dc](int result)
                    {
                        onRemoveRowsFromCurrentNode(dc, result);
                    }));
        }

        bool MainComponent::createWorkingSet()
        {
            if (!m_currentNode)
            {
                m_statusPanel.setStatusMessage("No data node selected to create working set from.", true);
                return false;
            }

            if (m_currentMainView == MainViewType::MixEditor)
            {
                m_statusPanel.setStatusMessage("Cannot create working set in Mix Editor view.", true);
                return false;
            }

            if (m_dataViewComponent.getNumSelectedRows() > 0)
            {
                return createWorkingSetFromTrackIds(m_dataViewComponent.getSelectedObjectIds());
            }
            else if (m_currentNode)
            {
                return createWorkingSetFromNode(m_currentNode);
            }
            m_statusPanel.setStatusMessage("Internal error: no selection, and no current node?", true);
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
                m_statusPanel.setStatusMessage("Working set '" + workingSetInfo.name + "' created successfully.", false);
                m_navigationTree.onWorkingSetCreated(workingSetInfo.id);
            }
            else
            {
                m_statusPanel.setStatusMessage("Failed to create working set: " + workingSetInfo.name, true);
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
                m_statusPanel.setStatusMessage("Error retrieving track count from node.", true);
                return false;
            }
            node->retain(REFCOUNT_DEBUG_ARGS); // Retain the node to ensure it stays valid during working set creation
            return onHandleCreateWorkingSetDialog(trackCount,
                [this, node](const juce::String &name)
                {
                    onCreateWorkingSetFromNodeCallback(name, node);
                    node->release(REFCOUNT_DEBUG_ARGS); // Release the node after working set creation
                });
        }

        bool MainComponent::onHandleCreateWorkingSetDialog(int64_t trackCount, OnCreateWorkingSetCallback callback)
        {
            if (trackCount <= 0)
            {
                m_statusPanel.setStatusMessage("No tracks available for working-set creation", true);
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
                m_statusPanel.setStatusMessage("Details not available for this item.", true);
            }
        }

        void MainComponent::onEditMixMetadata(INavigationNode *node)
        {
            if (auto *mixNode = dynamic_cast<MixNode *>(node))
            {
                const auto mixInfo = mixNode->getMixInfo();

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
                m_statusPanel.setStatusMessage("Details not available for this item.", true);
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
                    }));
        }

        void MainComponent::onDataActionDeleteConfirmed(INavigationNode *node, int result)
        {
            if (result == 1)
            {
                if (m_navigationTree.deleteObject(node))
                {
                    m_statusPanel.setStatusMessage(std::format("{} {} successfully removed.", node->m_refTypeNameForSingleObject, node->getName()));
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
                m_statusPanel.setStatusMessage(std::format("{} deletion cancelled.", node->m_refTypeNameForSingleObject), false);
            }
            node->release(REFCOUNT_DEBUG_ARGS); // Release the node after deletion
        }

        void MainComponent::createMix()
        {
            if (!m_currentNode)
            {
                m_statusPanel.setStatusMessage("No data node selected.", true);
                return;
            }
            if (m_currentMainView == MainViewType::MixEditor)
            {
                m_statusPanel.setStatusMessage("Cannot create mix in Mix Editor view.", true);
                return;
            }

            // Capture the source working set ID from the current node
            const WorkingSetId source_ws_id = m_currentNode->getUniqueId();

            std::vector<TrackInfo> selectedTracks{m_dataViewComponent.getSelectedTracks()};
            if (selectedTracks.size() <= 1)
            {
                selectedTracks = getAllTracks(m_currentNode);
            }

            if (selectedTracks.empty())
            {
                m_statusPanel.setStatusMessage("Not enough tracks selected to create a mix.", true);
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
                m_statusPanel.setStatusMessage("Mix '" + mixInfo.name + "' created successfully.", false);
                m_navigationTree.onMixCreated(mixInfo.mixId);
            }
            else
            {
                m_statusPanel.setStatusMessage("Failed to create mix: " + mixInfo.name, true);
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
            m_statusPanel.setStatusMessage("No track selected to play.", true);
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

                m_statusPanel.setStatusMessage("Scan dialog closed.", false);
            };

            launchOptions.launchAsync();
            m_statusPanel.setStatusMessage("Folder management dialog opened.", false);
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
                m_statusPanel.setStatusMessage("Column configuration not available in Mix Editor view.", true);
                return false;
            }
            TypedValueVector<DataViewColumnSection> *pConfigSection = nullptr;
            const auto currentNode = m_dataViewComponent.getCurrentNode();
            if (!currentNode)
            {
                m_statusPanel.setStatusMessage("No node selected at all.", true);
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
                m_statusPanel.setStatusMessage("No valid node selected for column configuration.", true);
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
                m_statusPanel.sendLookAndFeelChange();
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

        void MainComponent::showMarkerDialog(TrackId trackId, std::chrono::milliseconds position, bool isNewMarker)
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
                        m_statusPanel.setStatusMessage("Marker comment cannot be empty", true);
                        return;
                    }

                    MarkerId newMarkerId;
                    const auto result = markerManager.createMarker(trackId, position, comment, newMarkerId);

                    if (result == MarkerResult::Success)
                    {
                        spdlog::info("Created marker {} for track {} at {}ms", newMarkerId, trackId, position.count());
                        m_statusPanel.setStatusMessage("Marker created", false);

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
                        m_statusPanel.setStatusMessage("Failed to create marker", true);
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
                        m_statusPanel.setStatusMessage("Marker comment cannot be empty", true);
                        return;
                    }

                    const auto result = markerManager.updateMarker(marker.markerId, newComment);

                    if (result == MarkerResult::Success)
                    {
                        spdlog::info("Updated marker {}", marker.markerId);
                        m_statusPanel.setStatusMessage("Marker updated", false);

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
                        m_statusPanel.setStatusMessage("Failed to update marker", true);
                    }
                };

                // Delete callback
                dialog->onDelete = [this, dialog, &markerManager, marker, trackId]()
                {
                    const auto result = markerManager.deleteMarker(marker.markerId);

                    if (result == MarkerResult::Success)
                    {
                        spdlog::info("Deleted marker {}", marker.markerId);
                        m_statusPanel.setStatusMessage("Marker deleted", false);

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
                        m_statusPanel.setStatusMessage("Failed to delete marker", true);
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

    } // namespace ui
} // namespace jucyaudio
