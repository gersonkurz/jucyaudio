#pragma once

#include <Database/Includes/LibraryRootInfo.h> // Corrected include
#include <Database/TrackLibrary.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
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