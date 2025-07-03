#pragma once

#include <Database/Includes/INavigationNode.h> // For INavigationNode and DataColumn
#include <JuceHeader.h>
#include <UI/Settings.h> // Assuming this brings in DataViewColumnSection, TypedValueVector


namespace jucyaudio
{
    namespace ui
    {

        class ColumnConfigurationDialogComponent : public juce::Component, public juce::Button::Listener, public juce::ListBoxModel
        {
        public:
            // Callback when OK is pressed, signals if changes were made that require UI refresh
            using ColumnsConfiguredCallback = std::function<void(bool changesMade)>;

            ColumnConfigurationDialogComponent(
                const juce::String &viewName,                                                   // E.g., "Library View Columns", for title and context
                const std::vector<database::DataColumn> &allAvailableColumnsFromNode,           // All potential columns
                config::TypedValueVector<config::DataViewColumnSection> &columnsConfigToModify, // Reference to the config setting
                ColumnsConfiguredCallback onConfiguredCallback);
            ~ColumnConfigurationDialogComponent() override;

            void paint(juce::Graphics &g) override;
            void resized() override;

            // Button::Listener
            void buttonClicked(juce::Button *button) override;

            // ListBoxModel
            int getNumRows() override;
            void paintListBoxItem(int rowNumber, juce::Graphics &g, int width, int height, bool rowIsSelected) override;
            void listBoxItemClicked(int rowNumber, const juce::MouseEvent &event) override;
            // Optional: for drag-and-drop reordering within the dialog's listbox
            // var getDragSourceDescription(const SparseSet<int>& selectedRows) override;
            // bool canDrop(const var& description, ListBox& targetListBox, int insertIndex) override;
            // void itemDropped(const var& description, ListBox& targetListBox, int insertIndex) override;

        private:
            // Internal representation of a column being configured in the dialog
            struct ConfigurableColumn
            {
                const database::DataColumn *originalDataColumn; // Pointer to the source DataColumn
                bool isVisible;
                int currentWidth; // Might not be edited here, but could be shown
                int displayOrder; // Current order in this dialog's list
                // No need for originalColumnIndex here, as sqlId is the key

                ConfigurableColumn(const database::DataColumn *dc, bool vis, int width, int order)
                    : originalDataColumn(dc),
                      isVisible(vis),
                      currentWidth(width),
                      displayOrder(order)
                {
                }
            };

            void populateInternalColumnList(); // From allAvailableColumns and current config
            void applyChanges();               // Updates columnsConfigToModify and signals callback
            void resetToDefaults();

            // UI Elements
            juce::Label m_titleLabel;
            juce::ListBox m_columnsListBox;
            juce::TextButton m_moveUpButton;
            juce::TextButton m_moveDownButton;
            juce::TextButton m_okButton;
            juce::TextButton m_cancelButton;
            juce::TextButton m_resetButton;

            const juce::String m_viewName;
            const std::vector<database::DataColumn> &m_allAvailableColumns;                      // Reference
            config::TypedValueVector<config::DataViewColumnSection> &m_columnsConfigToModifyRef; // Reference to the actual config
            ColumnsConfiguredCallback m_onConfiguredCallback;

            // Internal list for managing columns in this dialog
            // This list will represent ALL available columns; 'isVisible' controls display,
            // and we'll sort it by displayOrder for visible items to show at top.
            std::vector<ConfigurableColumn> m_dialogColumnStates;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColumnConfigurationDialogComponent)
        };

    } // namespace ui
} // namespace jucyaudio
