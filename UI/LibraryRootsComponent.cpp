#include <Database/BackgroundService.h>
#include <UI/LibraryRootsComponent.h>
#include <UI/TaskDialog.h>
#include <UI/ThemeManager.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>

using namespace jucyaudio::database;

namespace jucyaudio
{
    namespace ui
    {
        namespace RootFolderTableColumns
        {
            enum
            {
                Path = 1
            };
        } // namespace RootFolderTableColumns

        // --- Task for running a scan ---
        // FIXED: This task now correctly takes FolderIds to match the TrackLibrary interface
        class ScanRootsTask final : public ILongRunningTask
        {
        public:
            ScanRootsTask(std::vector<FolderId> ids, bool forceRescan, bool removeMissingFiles, std::function<void()> onComplete)
                : ILongRunningTask{"Scanning Library Roots", false},
                  m_idsToScan{std::move(ids)},
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
            setSize(700, 500);

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
            m_rootFoldersTable.getHeader().addColumn("Library Root Path", RootFolderTableColumns::Path, 600, 50, 4000);
            m_rootFoldersTable.setMultipleSelectionEnabled(true);

            addAndMakeVisible(m_scanButton);
            m_scanButton.addListener(this);
            m_scanButton.setButtonText("Scan Selected Roots");
            m_scanButton.setEnabled(false);

            addAndMakeVisible(m_titleLabel);
            m_titleLabel.setFont(juce::Font{24.0f, juce::Font::bold});
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
            m_displayedRoots = m_rootManager.getAllRoots();
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

            if (columnId == RootFolderTableColumns::Path)
            {
                const auto &rootInfo = m_displayedRoots[rowNumber];
                g.setColour(getLookAndFeel().findColour(juce::ListBox::textColourId));
                g.drawText(jucePathFromFs(rootInfo.path), 2, 0, width - 4, height, juce::Justification::centredLeft, true);
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
                m_rootFoldersTable.updateContent();
            }
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
            auto &folderDb = m_db.getFolderDatabase();

            // FIXED: Correctly iterate over juce::SparseSet
            for (int i = 0; i < selectedRows.size(); ++i)
            {
                const int row = selectedRows[i];
                if (row >= 0 && static_cast<size_t>(row) < m_displayedRoots.size())
                {
                    // Find the FolderId associated with this root path.
                    // The path is guaranteed to exist in the roots table, so findOrCreate will find it.
                    const auto &path = m_displayedRoots[row].path;
                    FolderId id = folderDb.findOrCreateFolderByPath(path);
                    if (id != -1)
                    {
                        idsToScan.push_back(id);
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
                    });
            };

            const bool force = m_forceRescanCheckbox.getToggleState();
            const bool shouldRemove = m_removeMissingFilesToggle.getToggleState();
            auto *task = new ScanRootsTask(std::move(idsToScan), force, shouldRemove, onScanCompleteCallback);

            TaskDialog::launch("Scanning Library", task, 500, this);
        }
    } // namespace ui
} // namespace jucyaudio