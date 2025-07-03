#pragma once
#include <Database/Includes/FolderInfo.h>
#include <Database/Includes/IFolderDatabase.h>
#include <Database/TrackLibrary.h>
#include <JuceHeader.h>

namespace jucyaudio
{
    namespace ui
    {

        class ScanDialogComponent : public juce::Component,
                                    public juce::Button::Listener,
                                    public juce::TableListBoxModel
        {
        public:
            ScanDialogComponent(database::TrackLibrary &trackLib);
            ~ScanDialogComponent();

            void paint(juce::Graphics &g) override;
            void resized() override;

            // Button::Listener
            void buttonClicked(juce::Button *button) override;

            std::function<void()> onDialogClosed;

            // TableListBoxModel (for m_folderListTable)
            int getNumRows() override;
            void paintRowBackground(juce::Graphics &g, int rowNumber, int width,
                                    int height, bool rowIsSelected) override;
            void paintCell(juce::Graphics &g, int rowNumber, int columnId,
                           int width, int height, bool rowIsSelected) override;

        private:
            std::vector<database::FolderInfo> getSelectedFolderInfos() const;
            void sortOrderChanged(int newSortColumnId, bool isForwards) override;

            void addFolderToList();
            void removeSelectedFolders();
            void scanLibrary();
            void editSelectedFolder();
            void loadFolders(); // Load folders from TrackLibrary/Settings
            void saveFolders(); // Save changes back to TrackLibrary/Settings
            void selectedRowsChanged(int lastRowSelected) override;
            bool keyPressed(const juce::KeyPress &key) override;

            database::TrackLibrary &m_trackLibrary;
            database::IFolderDatabase &m_folderDatabase;

            // UI Elements
            juce::TextButton m_addFolderButton;
            juce::TextButton m_editFolderButton; // For later
            juce::TextButton m_deleteFolderButton;
            juce::ToggleButton m_forceRescanCheckbox;
            juce::TableListBox m_folderListTable;
            std::vector<database::FolderInfo>
                m_folders; // Data for the folder list table

            juce::TextButton m_scanStopButton{"Scan Now"};
            juce::Label m_titleLabel;
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScanDialogComponent)
        };

    } // namespace ui
} // namespace jucyaudio
