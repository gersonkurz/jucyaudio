#pragma once

#include <Database/BackgroundService.h>
#include <Database/Includes/LibraryRootInfo.h> // Corrected include
#include <Database/TrackLibrary.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
        /// @brief Scans the selected library roots, then records what it managed to scan.
        ///
        /// Declared here rather than in the .cpp so the self test can drive run() directly. The
        /// property worth pinning is that a scan which did not complete records nothing, and the only
        /// way to check that is to run the real thing - a test that reimplemented the decision would
        /// pass whatever run() did with it. Takes FolderIds, to match the TrackLibrary interface.
        class ScanRootsTask final : public database::ILongRunningTask
        {
        public:
            ScanRootsTask(std::vector<FolderId> ids,
                std::vector<database::LibraryRootId> rootIds,
                bool forceRescan,
                bool removeMissingFiles,
                std::function<void()> onComplete)
                : database::ILongRunningTask{"Scanning Library Roots", false},
                  m_idsToScan{std::move(ids)},
                  m_rootIdsToScan{std::move(rootIds)},
                  m_bForceRescan{forceRescan},
                  m_bRemoveMissingFiles{removeMissingFiles},
                  m_onComplete{std::move(onComplete)}
            {
            }

            void run(database::ProgressCallback progressCb, database::CompletionCallback completionCb, std::atomic<bool> &shouldCancel) override
            {
                if (!database::theBackgroundTaskService.pause())
                {
                    spdlog::warn("LibraryScanTask: Background service did not pause in time, proceeding anyway");
                }
                try
                {
                    const bool scanSucceeded =
                        database::theTrackLibrary.scanLibrary(m_idsToScan, m_bForceRescan, m_bRemoveMissingFiles, progressCb, completionCb, &shouldCancel);

                    // The answer decides whether anything is recorded. It used to be discarded, so a
                    // scan that was cancelled or refused still stamped every selected root with the
                    // current time, and the "Last Scanned" column said the opposite of what happened.
                    //
                    // Nothing is recorded rather than something partial, because scanLibrary reports
                    // one result for the whole batch: a root it finished before the failure cannot be
                    // told from one it never reached. Leaving every timestamp alone understates - the
                    // column keeps a date that was true - where stamping them overstates. The failure
                    // itself already reaches the user through completionCb, which holds the task
                    // dialog open with the message.
                    if (!scanSucceeded)
                    {
                        spdlog::warn("ScanRootsTask: the scan did not complete, so no root's last_scanned was updated");
                    }
                    else
                    {
                        // The scan invalidates the folder cache itself now, so the counts read below are the
                        // rebuilt ones. This used to do it here, which worked for this caller and only this
                        // caller - anything else that scanned got stale counts, and the invariant lived in the
                        // UI rather than with the thing that broke it.
                        auto &db = database::theTrackLibrary.getTrackDatabase();
                        auto &rootManager = db.getLibraryRootManager();
                        auto &folderDb = db.getFolderDatabase();

                        // The first of these rebuilds the cache; the rest are answered from it.
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
                }
                catch (const std::exception &e)
                {
                    spdlog::error("ScanRootsTask: Exception during scan: {}", e.what());
                }
                database::theBackgroundTaskService.resume();

                if (m_onComplete)
                {
                    juce::MessageManager::callAsync(m_onComplete);
                }
            }

        private:
            std::vector<FolderId> m_idsToScan;
            std::vector<database::LibraryRootId> m_rootIdsToScan;
            bool m_bForceRescan;
            bool m_bRemoveMissingFiles;
            std::function<void()> m_onComplete;
        };

        class LibraryRootsComponent : public juce::Component, public juce::Button::Listener, public juce::TableListBoxModel
        {
        public:
            LibraryRootsComponent();
            ~LibraryRootsComponent() override;

            void paint(juce::Graphics &g) override;
            void resized() override;
            void parentHierarchyChanged() override;
            void buttonClicked(juce::Button *button) override;

            std::function<void()> onDialogClosed;
            std::function<void()> onScanCompleted;

            int getNumRows() override;
            void paintRowBackground(juce::Graphics &g, int rowNumber, int width, int height, bool rowIsSelected) override;
            void paintCell(juce::Graphics &g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;

        private:
            void sortOrderChanged(int newSortColumnId, bool isForwards) override;
            void selectedRowsChanged(int lastRowSelected) override;
            bool keyPressed(const juce::KeyPress &key) override;

            void addLibraryRoot();
            void relocateSelectedRoot();
            void removeSelectedRoots();
            void scanSelectedRoots();
            void loadRoots();

            database::ITrackDatabase &m_db;
            database::ILibraryRootManager &m_rootManager;

            juce::LookAndFeel_V4 m_lookAndFeel;

            // UI Elements
            juce::TextButton m_addRootButton;
            juce::TextButton m_relocateRootButton;
            juce::TextButton m_removeRootButton;
            juce::ToggleButton m_forceRescanCheckbox;
            juce::ToggleButton m_removeMissingFilesToggle;
            juce::TableListBox m_rootFoldersTable;
            juce::TextButton m_scanButton;
            juce::TextButton m_refreshStatusButton;
            juce::Label m_titleLabel;

            // CORRECTED: The data source is simply a vector of the root info objects.
            std::vector<database::LibraryRootInfo> m_displayedRoots;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryRootsComponent)
        };
    } // namespace ui
} // namespace jucyaudio