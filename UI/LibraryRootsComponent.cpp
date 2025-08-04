#include <UI/LibraryRootsComponent.h>
#include <Database/BackgroundService.h>
#include <UI/TaskDialog.h>
#include <Utils/AssortedUtils.h>
#include <UI/ThemeManager.h>

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
            }; // For now, we only show the path.
        }

        LibraryRootsComponent::LibraryRootsComponent()
            : m_db{*theTrackLibrary.getTrackDatabase()},
              m_folderDb{m_db.getFolderDatabase()},
              m_rootFoldersTable{"rootFoldersTable", this},
              m_titleLabel{"titleLabel", "Library Roots"}
        {
            theThemeManager.applyCurrentTheme(m_lookAndFeel, this);
            setSize(700, 500);

            // --- Button Setup ---
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

            // --- Table Setup ---
            addAndMakeVisible(m_rootFoldersTable);
            m_rootFoldersTable.setHeaderHeight(25);
            m_rootFoldersTable.getHeader().addColumn("Library Root Path", RootFolderTableColumns::Path, 600, 50, 4000);
            m_rootFoldersTable.setMultipleSelectionEnabled(true);

            // --- Scan Button ---
            addAndMakeVisible(m_scanButton);
            m_scanButton.addListener(this);
            m_scanButton.setButtonText("Scan Selected Roots");
            m_scanButton.setEnabled(false);

            addAndMakeVisible(m_titleLabel);
            m_titleLabel.setFont(juce::Font{24.0f, juce::Font::bold});
            m_titleLabel.setJustificationType(juce::Justification::left);

            loadRootFolders();
        }

        LibraryRootsComponent::~LibraryRootsComponent()
        {
            setLookAndFeel(nullptr);
        }

        void LibraryRootsComponent::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        }

        void LibraryRootsComponent::resized()
        { /* ... Implement layout ... */
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

        // --- THIS IS THE NEWLY ADDED FUNCTION ---
        void LibraryRootsComponent::sortOrderChanged(int newSortColumnId, bool isForwards)
        {
            if (newSortColumnId == RootFolderTableColumns::Path)
            {
                // Create a comparator lambda that captures `this` to access the database object.
                auto comparator = [this, isForwards](const FolderInfo &a, const FolderInfo &b)
                {
                    // Reconstruct the full paths for comparison. This is fast due to the folder cache.
                    auto pathA = m_db.reconstructFullPath(a.folderId);
                    auto pathB = m_db.reconstructFullPath(b.folderId);

                    // For robust, case-insensitive sorting, we can use our normalization function.
                    auto normA = normalizeForCache(pathToString(pathA));
                    auto normB = normalizeForCache(pathToString(pathB));

                    if (normA && normB)
                    {
                        return isForwards ? (*normA < *normB) : (*normB < *normA);
                    }

                    // Fallback to simple path comparison if normalization fails
                    return isForwards ? (pathA < pathB) : (pathB < pathA);
                };

                std::sort(m_rootFolders.begin(), m_rootFolders.end(), comparator);

                m_rootFoldersTable.updateContent();
                m_rootFoldersTable.repaint();
            }
        }

        void LibraryRootsComponent::loadRootFolders()
        {
            // Use the new, correct IFolderDatabase method
            m_rootFolders = m_folderDb.getChildFolders(-1);
            m_rootFoldersTable.updateContent();
            m_rootFoldersTable.repaint();
            selectedRowsChanged(-1); // Update button states
        }

        int LibraryRootsComponent::getNumRows()
        {
            return static_cast<int>(m_rootFolders.size());
        }

        void LibraryRootsComponent::paintRowBackground(juce::Graphics &g, int, int, int, bool rowIsSelected)
        {
            if (rowIsSelected)
                g.fillAll(getLookAndFeel().findColour(juce::TextEditor::highlightColourId).withAlpha(0.4f));
        }

        void LibraryRootsComponent::paintCell(juce::Graphics &g, int rowNumber, int, int width, int height, bool)
        {
            if (rowNumber < 0 || static_cast<size_t>(rowNumber) >= m_rootFolders.size())
                return;

            const auto &folderInfo = m_rootFolders[rowNumber];

            // We need to reconstruct the full path for display
            auto fullPath = m_db.reconstructFullPath(folderInfo.folderId);

            g.setColour(getLookAndFeel().findColour(juce::ListBox::textColourId));
            g.drawText(pathToString(fullPath), 2, 0, width - 4, height, juce::Justification::centredLeft, true);
        }

        void LibraryRootsComponent::selectedRowsChanged(int)
        {
            const int numSelected = m_rootFoldersTable.getNumSelectedRows();
            m_removeRootButton.setEnabled(numSelected > 0);
            m_scanButton.setEnabled(numSelected > 0);
            m_relocateRootButton.setEnabled(numSelected == 1);
        }

        void LibraryRootsComponent::addLibraryRoot()
        {
            juce::FileChooser fc("Select Library Root Folder", juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*", true);

            if (fc.browseForDirectory())
            {
                auto chosenDir = fc.getResult();
                auto newPath = std::filesystem::path(chosenDir.getFullPathName().toStdString());

                // This is now a complex operation handled by the database layer
                FolderId newFolderId = m_folderDb.findOrCreateFolderByPath(newPath);

                if (newFolderId != -1)
                {
                    loadRootFolders();
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", "Could not add library root.");
                }
            }
        }

        void LibraryRootsComponent::removeSelectedRoots()
        {
            auto selectedRows = m_rootFoldersTable.getSelectedRows();
            if (selectedRows.isEmpty())
                return;

            if (juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon,
                    "Confirm Deletion",
                    "Are you sure you want to remove the selected library root(s)? This will remove all associated folders and tracks from the database. This "
                    "action cannot be undone."))
            {
                for (int i = 0; i < selectedRows.size(); ++i)
                {
                    int row = selectedRows[i];
                    if (row >= 0 && static_cast<size_t>(row) < m_rootFolders.size())
                    {
                        m_folderDb.removeFolder(m_rootFolders[row].folderId);
                    }
                }
                loadRootFolders();
            }
        }

        void LibraryRootsComponent::relocateSelectedRoot()
        {
            // TODO: This is a complex operation.
            // 1. Get selected FolderInfo.
            // 2. Open file chooser for new location.
            // 3. Update the `root_path` field of the FolderInfo in the database via IFolderDatabase::updateFolder.
            // 4. Invalidate the folder cache.
            // 5. Reload the list.
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Not Implemented", "Relocate functionality is not yet implemented.");
        }

        class ScanRootsTask final : public jucyaudio::database::ILongRunningTask
        {
        public:
            ScanRootsTask(std::vector<jucyaudio::FolderId> folderIds, bool forceRescan)
                : ILongRunningTask{"Scanning Library Roots", false},
                  m_folderIdsToScan{std::move(folderIds)},
                  m_bForceRescan{forceRescan}
            {
            }

            void run(ProgressCallback progressCb, CompletionCallback completionCb, std::atomic<bool> &shouldCancel) override
            {
                // Pause other background tasks like BPM analysis while scanning.
                jucyaudio::database::theBackgroundTaskService.pause();
                try
                {
                    // Call the library's main scan function with the correct parameters.
                    jucyaudio::database::theTrackLibrary.scanLibrary(m_folderIdsToScan, m_bForceRescan, progressCb, completionCb, &shouldCancel);
                }
                catch (const std::exception &e)
                {
                    spdlog::error("ScanRootsTask: Exception during scan: {}", e.what());
                }
                // Resume other background tasks.
                jucyaudio::database::theBackgroundTaskService.resume();
            }

        private:
            std::vector<jucyaudio::FolderId> m_folderIdsToScan;
            bool m_bForceRescan;
        };

        void LibraryRootsComponent::scanSelectedRoots()
        {
            auto selectedRows = m_rootFoldersTable.getSelectedRows();
            if (selectedRows.isEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "No Selection", "Please select one or more library roots to scan.");
                return;
            }

            std::vector<FolderId> idsToScan;
            for (int i = 0; i < selectedRows.size(); ++i)
            {
                int row = selectedRows[i];
                if (row >= 0 && static_cast<size_t>(row) < m_rootFolders.size())
                {
                    idsToScan.push_back(m_rootFolders[row].folderId);
                }
            }

            if (idsToScan.empty())
                return;

            // --- THIS IS THE CORRECTED LOGIC ---
            // Log the action for debugging. We need to manually format the vector.
            std::string idsString;
            for (size_t i = 0; i < idsToScan.size(); ++i)
            {
                idsString += std::to_string(idsToScan[i]);
                if (i < idsToScan.size() - 1)
                    idsString += ", ";
            }
            spdlog::info("Launching scan task for folder IDs: [{}]", idsString);

            // This is a callback that will be executed on the main thread after the task is done.
            // It will reload the folder list, which is important if the scan added/removed folders.
            auto onScanCompleteCallback = [this]()
            {
                juce::MessageManager::callAsync(
                    [this]()
                    {
                        this->loadRootFolders();
                    });
            };

            const bool force = m_forceRescanCheckbox.getToggleState();

            // Create and launch the new, correct task.
            // NOTE: The TaskDialog will take ownership of this pointer.
            auto *task = new ScanRootsTask(idsToScan, force);

            // The TaskDialog constructor now needs a completion callback.
            TaskDialog::launch("Scanning Library", task, 500, this, onScanCompleteCallback);
        }
    }
}
