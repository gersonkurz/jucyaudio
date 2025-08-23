#include <Database/BackgroundService.h>
#include <Database/Includes/IAlbumManager.h>
#include <Database/Sqlite/SqliteFolderDatabase.h>
#include <UI/CustomColourIds.h>
#include <UI/LibraryRootsComponent.h>
#include <UI/TaskDialog.h>
#include <UI/ThemeManager.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>
#include <UI/CheckboxLookAndFeel.h>
#include <ctime>
#include <iomanip>

using namespace jucyaudio::database;

namespace jucyaudio
{
    namespace ui
    {
        namespace RootFolderTableColumns
        {
            enum
            {
                Status = 1,
                Path = 2,
                FileCount = 3,
                LastScanned = 4
            };
        } // namespace RootFolderTableColumns

        // --- Task for running a scan ---
        // FIXED: This task now correctly takes FolderIds to match the TrackLibrary interface
        class ScanRootsTask final : public ILongRunningTask
        {
        public:
            ScanRootsTask(std::vector<FolderId> ids, std::vector<LibraryRootId> rootIds, bool forceRescan, bool removeMissingFiles, std::function<void()> onComplete)
                : ILongRunningTask{"Scanning Library Roots", false},
                  m_idsToScan{std::move(ids)},
                  m_rootIdsToScan{std::move(rootIds)},
                  m_bForceRescan{forceRescan},
                  m_bRemoveMissingFiles{removeMissingFiles},
                  m_onComplete{std::move(onComplete)}
            {
            }

            void run(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel) override
            {
                theBackgroundTaskService.pause();
                try
                {
                    // FIXED: This call now matches the expected signature
                    theTrackLibrary.scanLibrary(m_idsToScan, m_bForceRescan, m_bRemoveMissingFiles, progressCb, completionCb, &shouldCancel);
                    
                    // After scan completes, invalidate folder cache to recalculate track counts
                    auto &db = *theTrackLibrary.getTrackDatabase();
                    auto &rootManager = db.getLibraryRootManager();
                    auto &folderDb = db.getFolderDatabase();
                    //auto &albumManager = db.getAlbumManager();
                    
                    // Force the folder database to rebuild its cache with updated track counts
                    folderDb.invalidateCache();
                    
                    // Now get the updated track counts from the rebuilt cache
                    for (size_t i = 0; i < m_rootIdsToScan.size(); ++i)
                    {
                        if (i < m_idsToScan.size())
                        {
                            const auto rootId = m_rootIdsToScan[i];
                            const auto folderId = m_idsToScan[i];
                            
                            // Get the folder info which now has the correct recursive track count
                            const auto folderInfo = folderDb.getFolderById(folderId);
                            if (folderInfo.has_value())
                            {
                                const int64_t fileCount = folderInfo->trackCount;
                                
                                // Update the root's statistics
                                rootManager.updateScanStats(rootId);
                                spdlog::info("Updated root {} with {} files", rootId, fileCount);
                            }
                        }
                    }
                }
                catch (const std::exception &e)
                {
                    spdlog::error("ScanRootsTask: Exception during scan: {}", e.what());
                }
                theBackgroundTaskService.resume();

                if (m_onComplete)
                {
                    juce::MessageManager::callAsync(m_onComplete);
                }
            }

        private:
            std::vector<FolderId> m_idsToScan;
            std::vector<LibraryRootId> m_rootIdsToScan;
            bool m_bForceRescan;
            bool m_bRemoveMissingFiles;
            std::function<void()> m_onComplete;
        };

        LibraryRootsComponent::LibraryRootsComponent()
            : m_db{*theTrackLibrary.getTrackDatabase()},
              m_rootManager{m_db.getLibraryRootManager()},
              m_rootFoldersTable{"rootFoldersTable", this},
              m_titleLabel{"titleLabel", "Library Roots"}
        {
            theThemeManager.applyCurrentTheme(m_lookAndFeel, this);
            setSize(800, 500);

            addAndMakeVisible(m_addRootButton);
            m_addRootButton.setButtonText("Add Root...");
            m_addRootButton.addListener(this);

            addAndMakeVisible(m_relocateRootButton);
            m_relocateRootButton.setButtonText("Relocate...");
            m_relocateRootButton.addListener(this);
            m_relocateRootButton.setEnabled(false);

            addAndMakeVisible(m_removeRootButton);
            m_removeRootButton.setButtonText("Remove");
            m_removeRootButton.addListener(this);
            m_removeRootButton.setEnabled(false);

            addAndMakeVisible(m_forceRescanCheckbox);
            m_forceRescanCheckbox.setButtonText("Force Rescan All Files");

            m_removeMissingFilesToggle.setButtonText("Remove Missing Files from DB");
            m_removeMissingFilesToggle.setTooltip(
                "If checked, any audio files that are in the database but not found on disk during this scan will be permanently deleted from the library.");
            addAndMakeVisible(m_removeMissingFilesToggle);

            addAndMakeVisible(m_rootFoldersTable);
            m_rootFoldersTable.setHeaderHeight(25);
            m_rootFoldersTable.getHeader().setSortColumnId(RootFolderTableColumns::Path, true);
            m_rootFoldersTable.getHeader().addColumn("Status", RootFolderTableColumns::Status, 60, 50, 80);
            m_rootFoldersTable.getHeader().addColumn("Library Root Path", RootFolderTableColumns::Path, 340, 50, 2000);
            m_rootFoldersTable.getHeader().addColumn("Files", RootFolderTableColumns::FileCount, 80, 50, 120);
            m_rootFoldersTable.getHeader().addColumn("Last Scanned", RootFolderTableColumns::LastScanned, 150, 100, 200);
            m_rootFoldersTable.setMultipleSelectionEnabled(true);

            addAndMakeVisible(m_scanButton);
            m_scanButton.addListener(this);
            m_scanButton.setButtonText("Scan Selected Roots");
            m_scanButton.setEnabled(false);

            addAndMakeVisible(m_refreshStatusButton);
            m_refreshStatusButton.addListener(this);
            m_refreshStatusButton.setButtonText("Refresh Status");
            m_refreshStatusButton.setTooltip("Check which library roots are currently online/offline");

            addAndMakeVisible(m_titleLabel);
            m_titleLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(24.0f)}.boldened());
            m_titleLabel.setJustificationType(juce::Justification::left);

            setWantsKeyboardFocus(true);
            loadRoots();
        }

        LibraryRootsComponent::~LibraryRootsComponent()
        {
            setLookAndFeel(nullptr);
            if (onDialogClosed)
            {
                juce::MessageManager::callAsync(onDialogClosed);
            }
        }

        void LibraryRootsComponent::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        }

        void LibraryRootsComponent::resized()
        {
            juce::Rectangle<int> area = getLocalBounds().reduced(10);

            m_titleLabel.setBounds(area.removeFromTop(30));
            area.removeFromTop(10);

            juce::Rectangle<int> bottomPanel = area.removeFromBottom(40);
            juce::Rectangle<int> leftPanel = area.removeFromLeft(150);
            area.removeFromLeft(10);
            m_rootFoldersTable.setBounds(area);

            int buttonHeight = 25;
            int vMargin = 5;
            m_addRootButton.setBounds(leftPanel.removeFromTop(buttonHeight));
            leftPanel.removeFromTop(vMargin);
            m_relocateRootButton.setBounds(leftPanel.removeFromTop(buttonHeight));
            leftPanel.removeFromTop(vMargin);
            m_removeRootButton.setBounds(leftPanel.removeFromTop(buttonHeight));

            // Create a panel on the left side of the bottom bar for our toggles.
            juce::Rectangle<int> togglesPanel = bottomPanel.removeFromLeft(180);

            // Split that panel into two vertical halves for the toggles.
            m_forceRescanCheckbox.setBounds(togglesPanel.removeFromTop(togglesPanel.getHeight() / 2));
            m_removeMissingFilesToggle.setBounds(togglesPanel); // Takes the remaining half
            // --- END MODIFIED SECTION ---

            m_scanButton.setBounds(bottomPanel.removeFromRight(150).reduced(0, 5));
            bottomPanel.removeFromRight(5);
            m_refreshStatusButton.setBounds(bottomPanel.removeFromRight(120).reduced(0, 5));
        }
        
        void LibraryRootsComponent::parentHierarchyChanged()
        {
            m_forceRescanCheckbox.setLookAndFeel(CheckboxLookAndFeel::getInstance());
            m_removeMissingFilesToggle.setLookAndFeel(CheckboxLookAndFeel::getInstance());
        }

        void LibraryRootsComponent::buttonClicked(juce::Button *button)
        {
            if (button == &m_addRootButton)
                addLibraryRoot();
            else if (button == &m_relocateRootButton)
                relocateSelectedRoot();
            else if (button == &m_removeRootButton)
                removeSelectedRoots();
            else if (button == &m_scanButton)
                scanSelectedRoots();
            else if (button == &m_refreshStatusButton)
            {
                // Refresh the online/offline status of all roots
                m_rootManager.refreshRootStatuses();
                
                // Rebuild the offline folders table
                auto& folderDb = theTrackLibrary.getTrackDatabase()->getFolderDatabase();
                dynamic_cast<database::SqliteFolderDatabase&>(folderDb).rebuildOfflineFoldersTable(m_rootManager);
                
                loadRoots();  // Reload the display to show updated status
            }
        }

        bool LibraryRootsComponent::keyPressed(const juce::KeyPress &key)
        {
            if (key == juce::KeyPress::escapeKey)
            {
                if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
                {
                    dw->exitModalState(0);
                    return true;
                }
            }
            return false;
        }

        void LibraryRootsComponent::loadRoots()
        {
            spdlog::info("LibraryRootsComponent::loadRoots called");
            m_displayedRoots = m_rootManager.getAllRoots();
            
            spdlog::info("  Loaded {} roots:", m_displayedRoots.size());
            for (const auto& root : m_displayedRoots)
            {
                spdlog::info("    Root ID {} ({}): isOnline = {}", 
                            root.id, root.path, root.isOnline);
            }
            
            sortOrderChanged(m_rootFoldersTable.getHeader().getSortColumnId(), m_rootFoldersTable.getHeader().isSortedForwards());
            m_rootFoldersTable.updateContent();
            m_rootFoldersTable.repaint();
            selectedRowsChanged(-1);
        }

        void LibraryRootsComponent::addLibraryRoot()
        {
            juce::FileChooser fc("Select Library Root Folder", juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*", true);
            if (fc.browseForDirectory())
            {
                auto chosenDir = fc.getResult();
                if (m_rootManager.addRoot(chosenDir.getFullPathName().toStdString()))
                {
                    loadRoots();
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, "Could Not Add Root", "The selected folder may already be in the list.");
                }
            }
        }

        void LibraryRootsComponent::removeSelectedRoots()
        {
            auto selectedRows = m_rootFoldersTable.getSelectedRows();
            if (selectedRows.isEmpty())
                return;

            if (juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon,
                    "Confirm Removal",
                    "Are you sure you want to remove the selected path(s) from the list of Library Roots?\n\nThis will not delete any files from your disk or "
                    "library, but may hide them from the folder view."))
            {
                std::vector<LibraryRootId> idsToRemove;
                // FIXED: Correctly iterate over juce::SparseSet
                for (int i = 0; i < selectedRows.size(); ++i)
                {
                    const int row = selectedRows[i];
                    if (row >= 0 && static_cast<size_t>(row) < m_displayedRoots.size())
                    {
                        idsToRemove.push_back(m_displayedRoots[row].id);
                    }
                }
                for (const auto id : idsToRemove)
                {
                    m_rootManager.removeRoot(id);
                }
                loadRoots();
            }
        }

        int LibraryRootsComponent::getNumRows()
        {
            return static_cast<int>(m_displayedRoots.size());
        }

        void LibraryRootsComponent::paintRowBackground(juce::Graphics &g, int, int, int, bool rowIsSelected)
        {
            if (rowIsSelected)
                g.fillAll(getLookAndFeel().findColour(juce::TextEditor::highlightColourId).withAlpha(0.4f));
        }

        void LibraryRootsComponent::paintCell(juce::Graphics &g, int rowNumber, int columnId, int width, int height, bool)
        {
            if (rowNumber < 0 || static_cast<size_t>(rowNumber) >= m_displayedRoots.size())
                return;

            const auto &rootInfo = m_displayedRoots[rowNumber];
            
            // Use different text color based on online/offline status
            const auto textColour = rootInfo.isOnline 
                ? getLookAndFeel().findColour(ui::folderOnlineTextColourId)
                : getLookAndFeel().findColour(ui::folderOfflineTextColourId);
            g.setColour(textColour);
            
            // Log what we're painting
            if (columnId == RootFolderTableColumns::Status)
            {
                spdlog::debug("LibraryRootsComponent painting row {}: {} (ID {}) -> isOnline = {}", 
                             rowNumber, rootInfo.path, rootInfo.id, rootInfo.isOnline);
            }
            
            if (columnId == RootFolderTableColumns::Status)
            {
                // Draw a status indicator
                const auto indicatorSize = juce::jmin(width, height) * 0.4f;
                const auto x = (width - indicatorSize) * 0.5f;
                const auto y = (height - indicatorSize) * 0.5f;
                
                if (rootInfo.isOnline)
                {
                    // Green circle for online
                    g.setColour(juce::Colours::limegreen);
                    g.fillEllipse(x, y, indicatorSize, indicatorSize);
                }
                else
                {
                    // Gray circle for offline
                    g.setColour(juce::Colours::grey);
                    g.fillEllipse(x, y, indicatorSize, indicatorSize);
                }
                // Restore text color for other columns
                g.setColour(textColour);
            }
            else if (columnId == RootFolderTableColumns::Path)
            {
                g.drawText(jucePathFromFs(rootInfo.path), 2, 0, width - 4, height, juce::Justification::centredLeft, true);
            }
            else if (columnId == RootFolderTableColumns::FileCount)
            {
                const auto fileCountStr = std::format("{:L}", rootInfo.folderInfo.trackCount);
                g.drawText(fileCountStr, 2, 0, width - 4, height, juce::Justification::centredLeft, true);
            }
            else if (columnId == RootFolderTableColumns::LastScanned)
            {
                juce::String lastScannedStr = "Never";
                if (rootInfo.lastScanned.has_value())
                {
                    const auto time = std::chrono::system_clock::to_time_t(rootInfo.lastScanned.value());
                    char buffer[100];
                    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", std::localtime(&time));
                    lastScannedStr = buffer;
                }
                g.drawText(lastScannedStr, 2, 0, width - 4, height, juce::Justification::centredLeft, true);
            }
        }

        void LibraryRootsComponent::sortOrderChanged(int newSortColumnId, bool isForwards)
        {
            if (newSortColumnId == RootFolderTableColumns::Path)
            {
                std::sort(m_displayedRoots.begin(),
                    m_displayedRoots.end(),
                    [isForwards](const auto &a, const auto &b)
                    {
                        if (isForwards)
                            return a.path < b.path;
                        return b.path < a.path;
                    });
            }
            else if (newSortColumnId == RootFolderTableColumns::FileCount)
            {
                std::sort(m_displayedRoots.begin(),
                    m_displayedRoots.end(),
                    [isForwards](const auto &a, const auto &b)
                    {
                        
                        if (isForwards)
                            return a.folderInfo.trackCount < b.folderInfo.trackCount;
                        return b.folderInfo.trackCount < a.folderInfo.trackCount;
                    });
            }
            else if (newSortColumnId == RootFolderTableColumns::LastScanned)
            {
                std::sort(m_displayedRoots.begin(),
                    m_displayedRoots.end(),
                    [isForwards](const auto &a, const auto &b)
                    {
                        // Treat nullopt (never scanned) as the earliest possible time
                        const auto aTime = a.lastScanned.value_or(std::chrono::system_clock::time_point::min());
                        const auto bTime = b.lastScanned.value_or(std::chrono::system_clock::time_point::min());
                        if (isForwards)
                            return aTime < bTime;
                        return bTime < aTime;
                    });
            }
            m_rootFoldersTable.updateContent();
        }

        void LibraryRootsComponent::selectedRowsChanged(int)
        {
            const int numSelected = m_rootFoldersTable.getNumSelectedRows();
            m_removeRootButton.setEnabled(numSelected > 0);
            m_scanButton.setEnabled(numSelected > 0);
            m_relocateRootButton.setEnabled(numSelected == 1);
        }

        void LibraryRootsComponent::relocateSelectedRoot()
        {
            // This is still a complex operation that needs to be designed.
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Not Implemented", "Relocate functionality is not yet implemented.");
        }

        void LibraryRootsComponent::scanSelectedRoots()
        {
            auto selectedRows = m_rootFoldersTable.getSelectedRows();
            if (selectedRows.isEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "No Selection", "Please select one or more library roots to scan.");
                return;
            }

            std::vector<FolderId> idsToScan;
            std::vector<LibraryRootId> rootIdsToScan;
            auto &folderDb = m_db.getFolderDatabase();

            // FIXED: Correctly iterate over juce::SparseSet
            for (int i = 0; i < selectedRows.size(); ++i)
            {
                const int row = selectedRows[i];
                if (row >= 0 && static_cast<size_t>(row) < m_displayedRoots.size())
                {
                    // Find the FolderId associated with this root path.
                    // The path is guaranteed to exist in the roots table, so findOrCreate will find it.
                    const auto &rootInfo = m_displayedRoots[row];
                    FolderId id = folderDb.findOrCreateFolderByPath(rootInfo.path);
                    if (id != -1)
                    {
                        idsToScan.push_back(id);
                        rootIdsToScan.push_back(rootInfo.id);
                    }
                }
            }

            if (idsToScan.empty())
                return;

            auto onScanCompleteCallback = [this]()
            {
                juce::MessageManager::callAsync(
                    [this]()
                    {
                        this->loadRoots();
                        if (this->onScanCompleted)
                        {
                            this->onScanCompleted();
                        }
                    });
            };

            const bool force = m_forceRescanCheckbox.getToggleState();
            const bool shouldRemove = m_removeMissingFilesToggle.getToggleState();
            auto *task = new ScanRootsTask{std::move(idsToScan), std::move(rootIdsToScan), force, shouldRemove, onScanCompleteCallback};

            TaskDialog::launch("Scanning Library", task, 500, this);
        }
    } // namespace ui
} // namespace jucyaudio