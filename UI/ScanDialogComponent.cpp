#include <UI/MainComponent.h>
#include <UI/ScanDialogComponent.h>
#include <UI/TaskDialog.h>
#include <Utils/AssortedUtils.h>
#include <Database/BackgroundService.h>
#include <Utils/UiUtils.h>

using namespace jucyaudio::database;

namespace jucyaudio
{
    namespace ui
    {

        // Define Column IDs for the Folder Table
        namespace FolderTableColumnIDs
        {
            enum
            {
                Path = 1,
                FileCount,
                TotalSize,
                LastScanned
                // Add Status later if needed
            };
        } // namespace FolderTableColumnIDs

        ScanDialogComponent::ScanDialogComponent()
            : m_folderDatabase{theTrackLibrary.getFolderDatabase()},
              m_folderListTable{"foldersTable", this},
              m_titleLabel{"titleLabel", "Scan Folders"}
        {
            theThemeManager.applyCurrentTheme(m_lookAndFeel, this);

            setSize(700, 500); // Set a reasonable initial size for the dialog

            // --- Button Setup ---
            addAndMakeVisible(m_addFolderButton);
            m_addFolderButton.setButtonText("Add Folder...");
            m_addFolderButton.addListener(this);

            addAndMakeVisible(m_editFolderButton);
            m_editFolderButton.setButtonText("Edit Selected");
            m_editFolderButton.addListener(this);
            m_editFolderButton.setEnabled(false); // Enabled when a row is selected

            addAndMakeVisible(m_deleteFolderButton);
            m_deleteFolderButton.setButtonText("Remove Selected");
            m_deleteFolderButton.addListener(this);
            m_deleteFolderButton.setEnabled(false); // Enabled when a row is selected

            addAndMakeVisible(m_forceRescanCheckbox);
            m_forceRescanCheckbox.setButtonText("Force Rescan"); // Or just global "Force Rescan"

            // --- Folder List Table Setup ---
            addAndMakeVisible(m_folderListTable);
            m_folderListTable.setHeaderHeight(25);
            m_folderListTable.getHeader().setSortColumnId(FolderTableColumnIDs::Path, true);
            m_folderListTable.getHeader().addColumn("Path", FolderTableColumnIDs::Path, 350, 50, 4000);
            m_folderListTable.getHeader().addColumn("Count", FolderTableColumnIDs::FileCount, 80, 30, 150, juce::TableHeaderComponent::notResizable);
            m_folderListTable.getHeader().addColumn("Total Bytes", FolderTableColumnIDs::TotalSize, 100, 30, 200, juce::TableHeaderComponent::notResizable);
            m_folderListTable.getHeader().addColumn("Last Scanned", FolderTableColumnIDs::LastScanned, 120, 50, 250, juce::TableHeaderComponent::notResizable);
            m_folderListTable.setMultipleSelectionEnabled(true); // If you want to delete multiple folders at once

            // --- Scan Controls Setup ---
            addAndMakeVisible(m_scanStopButton);
            m_scanStopButton.addListener(this);
            m_scanStopButton.setButtonText("Scan Now");

            // --- Title Label Setup ---
            addAndMakeVisible(m_titleLabel);
            m_titleLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(24.0f)}.boldened()); // Adjust size as needed
            m_titleLabel.setJustificationType(juce::Justification::left);

            // --- Initial State & Data Load ---
            loadFolders(); // Populate the table
        }

        ScanDialogComponent::~ScanDialogComponent()
        {
            setLookAndFeel(nullptr);
            juce::MessageManager::callAsync(
                [callback = onDialogClosed]()
                {
                    callback();
                });
        }

        void ScanDialogComponent::sortOrderChanged(int newSortColumnId, bool isForwards)
        {
            spdlog::info("ScanDialog: Sort order changed. Column ID: {}, Ascending: {}", newSortColumnId, isForwards);

            auto get_comparator = [&]() -> std::function<bool(const FolderInfo &, const FolderInfo &)>
            {
                auto make_comparator = [&](auto field_selector)
                {
                    return [=](const FolderInfo &a, const FolderInfo &b)
                    {
                        const auto &field_a = field_selector(a);
                        const auto &field_b = field_selector(b);
                        return isForwards ? field_a < field_b : field_a > field_b;
                    };
                };

                switch (newSortColumnId)
                {
                case FolderTableColumnIDs::Path:
                    return make_comparator(
                        [](const auto &f) -> const auto &
                        {
                            return f.path;
                        });
                case FolderTableColumnIDs::FileCount:
                    return make_comparator(
                        [](const auto &f) -> const auto &
                        {
                            return f.numFiles;
                        });
                case FolderTableColumnIDs::TotalSize:
                    return make_comparator(
                        [](const auto &f) -> const auto &
                        {
                            return f.totalSizeBytes;
                        });
                case FolderTableColumnIDs::LastScanned:
                    return make_comparator(
                        [](const auto &f) -> const auto &
                        {
                            return f.lastScannedTime;
                        });
                default:
                    spdlog::error("ScanDialog: Unknown sort column ID: {}", newSortColumnId);
                    return [](const auto &, const auto &)
                    {
                        return false;
                    };
                }
            };

            std::sort(m_folders.begin(), m_folders.end(), get_comparator());

            m_folderListTable.updateContent();
            m_folderListTable.repaint(); // Ensure repaint
        }

        void ScanDialogComponent::paint(juce::Graphics &g)
        {
            // Fill background if needed, or DialogWindow will handle it
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        }

        void ScanDialogComponent::resized()
        {
            juce::Rectangle<int> area = getLocalBounds().reduced(10);

            // *** Allocate space for the title label at the top ***
            m_titleLabel.setBounds(area.removeFromTop(30)); // e.g., 30 pixels high
            area.removeFromTop(10);                               // Add some spacing below the title

            juce::Rectangle<int> leftButtonPanel = area.removeFromLeft(150);
            int buttonHeight = 25;
            int vMargin = 5; // Vertical margin between buttons
            m_addFolderButton.setBounds(leftButtonPanel.removeFromTop(buttonHeight));
            leftButtonPanel.removeFromTop(vMargin);
            m_editFolderButton.setBounds(leftButtonPanel.removeFromTop(buttonHeight)); // Make sure this is added and visible
            leftButtonPanel.removeFromTop(vMargin);
            m_deleteFolderButton.setBounds(leftButtonPanel.removeFromTop(buttonHeight));
            leftButtonPanel.removeFromTop(vMargin);
            m_forceRescanCheckbox.setBounds(leftButtonPanel.removeFromTop(buttonHeight));

            area.removeFromLeft(10);

            // --- Revised Bottom Panel Layout ---
            int bottomPanelHeight = 50; // Increased height for two rows
            juce::Rectangle<int> bottomPanel = area.removeFromBottom(bottomPanelHeight);
            m_folderListTable.setBounds(area);

            // Top row of bottom panel: Scan Button and Progress Bar
            auto topRowBottomPanel = bottomPanel.removeFromTop(bottomPanelHeight / 2);
            m_scanStopButton.setBounds(topRowBottomPanel.removeFromLeft(120).reduced(0, 2)); // Reduced vertical margin
            topRowBottomPanel.removeFromLeft(10);                                            // Margin
        }

        bool ScanDialogComponent::keyPressed(const juce::KeyPress &key)
        {
            if (key == juce::KeyPress::escapeKey)
            { // Already handled by DialogWindow if escapeKeyTriggersCloseButton
              // findParentComponentOfClass<juce::DialogWindow>()->exitModalState(0);
              // return true;
            }
#if JUCE_MAC
            if (key.getKeyCode() == 'q' && key.getModifiers().isCommandDown())
            {
                // We want the *application* to quit, not just close the dialog.
                // Closing the dialog first might be necessary if it holds
                // resources.
                if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
                {
                    dw->exitModalState(0); // Standard way to signal dialog
                                           // should close with return code 0
                }
                // Then, tell the application to quit
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
                return true;
            }
#endif
            return juce::Component::keyPressed(key); // Pass on other keys
        }

        void ScanDialogComponent::buttonClicked(juce::Button *button)
        {
            if (button == &m_addFolderButton)
            {
                addFolderToList();
            }
            else if (button == &m_editFolderButton)
            {
                editSelectedFolder();
            }
            else if (button == &m_deleteFolderButton)
            {
                removeSelectedFolders();
            }
            else if (button == &m_scanStopButton)
            {
                scanLibrary();
            }
        }

        // --- TableListBoxModel ---
        int ScanDialogComponent::getNumRows()
        {
            return static_cast<int>(m_folders.size());
        }

        void ScanDialogComponent::paintRowBackground(juce::Graphics &g, int /*rowNumber*/, int /*width*/, int /*height*/, bool rowIsSelected)
        {
            if (rowIsSelected)
            {
                g.fillAll(getLookAndFeel().findColour(juce::TextEditor::highlightColourId).withAlpha(0.4f));
            }
            // No alternating row color, or add it if desired
        }

        void ScanDialogComponent::paintCell(juce::Graphics &g, int rowNumber, int columnId, int width, int height, bool /*rowIsSelected*/)
        {
            if (rowNumber < 0 || static_cast<size_t>(rowNumber) >= m_folders.size())
                return; // Out of bounds, shouldn't happen but just in case

            const auto &folderInfo = m_folders[rowNumber];
            juce::String text;
            switch (columnId)
            {
            case FolderTableColumnIDs::Path:
                text = jucePathFromFs(folderInfo.path); // Use wstring for display on Windows,
                                                        // string for others
                break;
            case FolderTableColumnIDs::FileCount:
                text = folderInfo.isValid() ? formatWithThousandsSeparator(folderInfo.numFiles) : "?";
                break;
            case FolderTableColumnIDs::TotalSize:
                text = folderInfo.isValid() ? juce::File::descriptionOfSizeInBytes(folderInfo.totalSizeBytes) : "?";
                break;
            case FolderTableColumnIDs::LastScanned:
                text = folderInfo.isValid() ? timestampToString(folderInfo.lastScannedTime) : "?";
                break;
            default:
                break;
            }
            g.setColour(getLookAndFeel().findColour(juce::ListBox::textColourId));
            g.drawText(text, 2, 0, width - 4, height, juce::Justification::centredLeft, true);
        }

        void ScanDialogComponent::selectedRowsChanged([[maybe_unused]] int lastRowSelected)
        {
            const int numSelected = m_folderListTable.getNumSelectedRows();
            m_deleteFolderButton.setEnabled(numSelected > 0);
            m_editFolderButton.setEnabled(numSelected == 1);
        }

        // --- Private Helper Methods ---
        void ScanDialogComponent::addFolderToList()
        {
            juce::FileChooser fc("Select Folder to Add to Library",
                                 juce::File::getSpecialLocation(juce::File::userMusicDirectory), // Start in user's music
                                 "*",                                                            // Wildcard (not really used for directory chooser)
                                 true);                                                          // true = directory chooser

            if (fc.browseForDirectory())
            {
                juce::File chosenDir = fc.getResult();
                if (!chosenDir.exists() || !chosenDir.isDirectory())
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Invalid Selection",
                                                           "Please select a valid directory to add to the "
                                                           "library.");
                    return; // Exit if not a directory
                }
                std::u8string u8path = reinterpret_cast<const char8_t *>(chosenDir.getFullPathName().toRawUTF8());
                std::filesystem::path newPath{u8path};
                if (!std::filesystem::exists(newPath) || !std::filesystem::is_directory(newPath))
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Invalid Directory",
                                                           "The selected path is not a valid directory. Please "
                                                           "select a valid folder.");
                    return; // Exit if not a valid directory
                }

                // Check if already exists (case-insensitive path comparison
                // might be needed)
                bool exists = false;
                for (const auto &folder : m_folders)
                {
                    if (std::filesystem::equivalent(folder.path, newPath))
                    { // Robust check
                        exists = true;
                        break;
                    }
                }

                if (exists)
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Folder Exists", "This folder is already in the library scan list.");
                }
                else
                {
                    FolderInfo fi{};
                    fi.path = newPath;
                    if (m_folderDatabase.addFolder(fi))
                    {
                        loadFolders(); // Reload from DB to get fresh list &
                        
                    }
                    else
                    {
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error Adding Folder", "Could not add folder to library");
                    }
                }
            }
        }

        void ScanDialogComponent::removeSelectedFolders()
        {
            // member undefined
            // Corrected iteration for SparseSet
            juce::SparseSet<int> selectedRows = m_folderListTable.getSelectedRows();
            if (selectedRows.isEmpty())
                return;

            // To remove items from m_folders based on selectedRows, it's safer
            // to collect FolderIds to remove, then remove them, then reload. Or
            // iterate indices from highest to lowest.
            std::vector<int> rowsToRemove;
            for (int i = 0; i < selectedRows.size(); ++i)
            {
                rowsToRemove.push_back(selectedRows[i]);
            }
            // Sort rowsToRemove in descending order to avoid index shifting
            // issues if removing directly from m_folders
            std::sort(rowsToRemove.rbegin(), rowsToRemove.rend());

            for (int rowIndexToRemove : rowsToRemove)
            {
                if (rowIndexToRemove >= 0 && static_cast<size_t>(rowIndexToRemove) < m_folders.size())
                {
                    const auto &folderInfo = m_folders[rowIndexToRemove];

                    if(!removeFolderAndAssociatedTracks(folderInfo))
                    {
                        break;
                    }
                }
            }
            loadFolders();                       // Refresh list from DB
            m_folderListTable.deselectAllRows(); // Deselect after removal
            m_deleteFolderButton.setEnabled(false);
        }

        void ScanDialogComponent::editSelectedFolder()
        {
            if (m_folderListTable.getNumSelectedRows() == 1)
            {
                int selectedRow = m_folderListTable.getSelectedRow(); // Gets the single selected row
                if (selectedRow >= 0 && static_cast<size_t>(selectedRow) < m_folders.size())
                {
                }
            }
        }
        bool ScanDialogComponent::removeFolderAndAssociatedTracks(const database::FolderInfo &folderInfo)
        {
            if (!folderInfo.isValid())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Invalid Folder", "Cannot remove an invalid folder.");
                return false;
            }

            // Confirm deletion
            juce::String message = "Are you sure you want to remove the folder:\n\n" + jucePathFromFs(folderInfo.path) + "\n\nand all associated tracks?";
            if(!juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon, "Confirm Removal", message))
            {
                return false;
            }

            if (m_folderDatabase.removeFolder(folderInfo.folderId))
            {
                return true; // Successfully removed
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error Removing Folder", "Could not remove folder from library.");
                return false; // Failed to remove
            }
        }


        using UpdateUiCallback = std::function<void()>;

        class ScanFoldersTask final : public ILongRunningTask
        {
        public:
            ScanFoldersTask(std::vector<FolderInfo> foldersToScan, bool forceRescan, UpdateUiCallback updateUiCallback)
                : ILongRunningTask{"Scanning Files & Folders", false},
                  m_foldersToScan{foldersToScan},
                  m_updateUiCallback{updateUiCallback},
                  m_bForceRescan{forceRescan}
            {
            }

            void run(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel) override
            {
                theBackgroundTaskService.pause();
                try
                {
                    theTrackLibrary.scanLibrary(m_foldersToScan, m_bForceRescan, progressCb, completionCb, &shouldCancel);
                }
                catch(const std::exception& e)
                {
                    spdlog::error("ScanFoldersTask: Exception during scan: {}", e.what());
                }
                theBackgroundTaskService.resume();
                juce::MessageManager::callAsync(
                    [updateCallback = m_updateUiCallback]()
                    {
                        juce::Timer::callAfterDelay(100, updateCallback); // Small delay
                    });
            }

        private:
            std::vector<FolderInfo> m_foldersToScan;
            UpdateUiCallback m_updateUiCallback;
            bool m_bForceRescan;
        };

        void ScanDialogComponent::scanLibrary()
        {
            const auto folderInfosToScan{(m_folderListTable.getNumSelectedRows() > 0) ? getSelectedFolderInfos() : m_folders};

            if (folderInfosToScan.empty())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "No Folders", "Please add / select folders to scan first.");
            }
            else
            {
                auto updateUiCallback = [this]()
                {
                    juce::MessageManager::callAsync(
                        [this]()
                        {
                            this->loadFolders();
                        });
                };
                const bool force = m_forceRescanCheckbox.getToggleState();
                auto *task = new ScanFoldersTask{folderInfosToScan, force, updateUiCallback};
                TaskDialog::launch("Scanning in progress", task, 500, this);
                task->release(REFCOUNT_DEBUG_ARGS);
            }
        }

        void ScanDialogComponent::loadFolders()
        {
            m_folderDatabase.getFolders(m_folders);
            m_folderListTable.updateContent();
            m_folderListTable.repaint();
            m_deleteFolderButton.setEnabled(m_folderListTable.getNumSelectedRows() > 0);
        }

        std::vector<database::FolderInfo> ScanDialogComponent::getSelectedFolderInfos() const
        {
            std::vector<database::FolderInfo> selectedFolderInfos;
            juce::SparseSet<int> selectedRows = m_folderListTable.getSelectedRows();

            for (int i = 0; i < selectedRows.size(); ++i)
            {
                const int rowIndex = selectedRows[i];
                if (rowIndex >= 0 && static_cast<size_t>(rowIndex) < m_folders.size())
                {
                    selectedFolderInfos.push_back(m_folders[rowIndex]); // Add a copy of the FolderInfo
                }
            }
            return selectedFolderInfos;
        }
    } // namespace ui
} // namespace jucyaudio
