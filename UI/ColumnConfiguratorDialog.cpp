#include <UI/ColumnConfiguratorDialog.h>
#include <algorithm> // For std::sort, std::find_if
#include <UI/MainComponent.h>

namespace jucyaudio
{
    namespace ui
    {

        // Anonymous namespace for constants
        namespace
        {
            const int dialogWidth = 500;
            const int dialogHeight = 400;
            const int buttonWidth = 100;
            const int buttonHeight = 24;
            const int margin = 10;
        } // namespace

        ColumnConfigurationDialogComponent::ColumnConfigurationDialogComponent(const juce::String &viewName,
                                                                               const std::vector<database::DataColumn> &allAvailableColumnsFromNode,
                                                                               config::TypedValueVector<config::DataViewColumnSection> &columnsConfigToModify,
                                                                               ColumnsConfiguredCallback onConfiguredCallback)
            : m_titleLabel("title", "Configure Columns for " + viewName),
              m_columnsListBox("colsListBox", this),
              m_moveUpButton("Move Up"),
              m_moveDownButton("Move Down"),
              m_okButton("OK"),
              m_cancelButton("Cancel"),
              m_resetButton("Reset to Defaults"),
              m_viewName(viewName),
              m_allAvailableColumns(allAvailableColumnsFromNode),
              m_columnsConfigToModifyRef(columnsConfigToModify),
              m_onConfiguredCallback(std::move(onConfiguredCallback))
        {
            m_lookAndFeel.setColourScheme (getColourSchemeFromConfig());
            setLookAndFeel(&m_lookAndFeel); // Set custom LookAndFeel
            setSize(dialogWidth, dialogHeight);

            addAndMakeVisible(m_titleLabel);
            m_titleLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(24.0f)}.boldened());
            m_titleLabel.setJustificationType(juce::Justification::left);

            addAndMakeVisible(m_columnsListBox);
            m_columnsListBox.setOutlineThickness(1);

            addAndMakeVisible(m_moveUpButton);
            m_moveUpButton.addListener(this);
            m_moveUpButton.setEnabled(false);

            addAndMakeVisible(m_moveDownButton);
            m_moveDownButton.addListener(this);
            m_moveDownButton.setEnabled(false);

            addAndMakeVisible(m_okButton);
            m_okButton.addListener(this);

            addAndMakeVisible(m_cancelButton);
            m_cancelButton.addListener(this);

            addAndMakeVisible(m_resetButton);
            m_resetButton.addListener(this);

            populateInternalColumnList(); // Initialize m_dialogColumnStates
            m_columnsListBox.updateContent();
        }

        ColumnConfigurationDialogComponent::~ColumnConfigurationDialogComponent()
        {
            setLookAndFeel(nullptr);
            // m_columnsListBox.setModel(nullptr); // Good practice
        }

        void ColumnConfigurationDialogComponent::populateInternalColumnList()
        {
            m_dialogColumnStates.clear();

            // Get current configuration (or defaults if config is empty)
            // config::getFromConfig in DynamicColumnManager has logic for this.
            // We need to adapt that or replicate. For now, assume m_columnsConfigToModifyRef
            // is either populated or we build defaults.

            // If m_columnsConfigToModifyRef is empty, create a default based on allAvailableColumns
            if (m_columnsConfigToModifyRef.empty())
            {
                int defaultOrder = 0;
                for (const auto &availableCol : m_allAvailableColumns)
                {
                    // Assume all columns are visible by default if no config
                    // You might have a DataColumn::defaultVisible property
                    m_dialogColumnStates.emplace_back(&availableCol, true, availableCol.defaultWidth, defaultOrder++);
                }
            }
            else
            {
                // Build m_dialogColumnStates based on m_allAvailableColumns and the existing config
                // 1. Add all configured (visible) columns in their configured order
                int order = 0;
                for (const auto &configuredSectionPtr : m_columnsConfigToModifyRef.get())
                {
                    const auto &configuredSection = *configuredSectionPtr;
                    auto it = std::find_if(m_allAvailableColumns.begin(), m_allAvailableColumns.end(),
                                           [&](const database::DataColumn &dc)
                                           {
                                               return dc.name == configuredSection.columnName.get();
                                           });
                    if (it != m_allAvailableColumns.end())
                    {
                        m_dialogColumnStates.emplace_back(&(*it), true, configuredSection.columnWidth.get(), order++);
                    }
                }
                // 2. Add all other available (but not configured = not visible) columns at the end
                for (const auto &availableCol : m_allAvailableColumns)
                {
                    bool alreadyAdded = false;
                    for (const auto &dialogStateCol : m_dialogColumnStates)
                    {
                        if (dialogStateCol.originalDataColumn->name == availableCol.name)
                        {
                            alreadyAdded = true;
                            break;
                        }
                    }
                    if (!alreadyAdded)
                    {
                        m_dialogColumnStates.emplace_back(&availableCol, false, availableCol.defaultWidth, order++);
                    }
                }
            }
            // No need to sort m_dialogColumnStates here; its order IS the display order for the ListBox.
            // The ListBox will display them as they are in m_dialogColumnStates.
            // Visibility is handled by the checkbox in paintListBoxItem.
            // Reordering (Move Up/Down) will directly manipulate m_dialogColumnStates.
        }

        void ColumnConfigurationDialogComponent::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        }

        void ColumnConfigurationDialogComponent::resized()
        {
            juce::FlexBox fb;
            fb.flexDirection = juce::FlexBox::Direction::column;
            fb.alignItems = juce::FlexBox::AlignItems::stretch;

            fb.items.add(juce::FlexItem(m_titleLabel).withHeight(30.0f).withMargin(margin));

            juce::FlexBox listAndButtonsBox;
            listAndButtonsBox.flexDirection = juce::FlexBox::Direction::row;
            listAndButtonsBox.items.add(juce::FlexItem(m_columnsListBox).withFlex(1.0)); // List takes most space

            juce::FlexBox sideButtonsBox;
            sideButtonsBox.flexDirection = juce::FlexBox::Direction::column;
            sideButtonsBox.justifyContent = juce::FlexBox::JustifyContent::flexStart;
            sideButtonsBox.items.add(
                juce::FlexItem(m_moveUpButton).withHeight(buttonHeight).withWidth(buttonWidth).withMargin(juce::FlexItem::Margin(0, 0, 5, 5)));
            sideButtonsBox.items.add(
                juce::FlexItem(m_moveDownButton).withHeight(buttonHeight).withWidth(buttonWidth).withMargin(juce::FlexItem::Margin(0, 0, 0, 5)));
            listAndButtonsBox.items.add(juce::FlexItem(sideButtonsBox).withWidth(buttonWidth + 10));

            fb.items.add(juce::FlexItem(listAndButtonsBox).withFlex(1.0).withMargin(juce::FlexItem::Margin(0, margin, margin, margin)));

            juce::FlexBox bottomButtonsBox;
            bottomButtonsBox.justifyContent = juce::FlexBox::JustifyContent::flexEnd;
            bottomButtonsBox.items.add(
                juce::FlexItem(m_resetButton).withHeight(buttonHeight).withWidth(buttonWidth).withMargin(juce::FlexItem::Margin(0, 5, 0, 0)));
            bottomButtonsBox.items.add(
                juce::FlexItem(m_cancelButton).withHeight(buttonHeight).withWidth(buttonWidth).withMargin(juce::FlexItem::Margin(0, 5, 0, 0)));
            bottomButtonsBox.items.add(
                juce::FlexItem(m_okButton).withHeight(buttonHeight).withWidth(buttonWidth).withMargin(juce::FlexItem::Margin(0, 0, 0, 0))); // OK on far right
            fb.items.add(juce::FlexItem(bottomButtonsBox).withHeight(buttonHeight).withMargin(juce::FlexItem::Margin(0, margin, margin, margin)));

            fb.performLayout(getLocalBounds());
        }

        void ColumnConfigurationDialogComponent::buttonClicked(juce::Button *button)
        {
            if (button == &m_okButton)
            {
                applyChanges();
                if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
                    dw->exitModalState(1);
            }
            else if (button == &m_cancelButton)
            {
                if (m_onConfiguredCallback)
                    m_onConfiguredCallback(false); // No changes applied
                if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
                    dw->exitModalState(0);
            }
            else if (button == &m_resetButton)
            {
                resetToDefaults();
            }
            else if (button == &m_moveUpButton || button == &m_moveDownButton)
            {
                int selectedRow = m_columnsListBox.getSelectedRow();
                if (selectedRow != -1)
                {
                    int direction = (button == &m_moveUpButton) ? -1 : 1;
                    int newRow = selectedRow + direction;
                    if (newRow >= 0 && static_cast<size_t>(newRow) < m_dialogColumnStates.size())
                    {
                        // Only move among visible items, or allow moving into hidden?
                        // For simplicity, let's assume we only reorder within m_dialogColumnStates as a whole.
                        // The "displayOrder" field would be key if sorting visible ones separately.
                        // Direct swap in m_dialogColumnStates:
                        std::swap(m_dialogColumnStates[selectedRow], m_dialogColumnStates[newRow]);
                        m_columnsListBox.updateContent();
                        m_columnsListBox.selectRow(newRow); // Keep selection on moved item
                        m_columnsListBox.scrollToEnsureRowIsOnscreen(newRow);
                    }
                }
            }
        }

        // --- ListBoxModel ---
        int ColumnConfigurationDialogComponent::getNumRows()
        {
            return static_cast<int>(m_dialogColumnStates.size());
        }

        void ColumnConfigurationDialogComponent::paintListBoxItem(int rowNumber, juce::Graphics &g, int width, int height, bool rowIsSelected)
        {
            if (rowIsSelected)
            {
                g.fillAll(getLookAndFeel().findColour(juce::ListBox::outlineColourId).withAlpha(0.4f)); // Or selectedItemBackgroundColourId
            }

            const auto &colState = m_dialogColumnStates[rowNumber];

            // Checkbox for visibility
            juce::Rectangle<int> checkboxBounds(5, (height - 15) / 2, 15, 15);
            getLookAndFeel().drawTickBox(g, *this, checkboxBounds.getX(), checkboxBounds.getY(), checkboxBounds.getWidth(),
                                                                   checkboxBounds.getHeight(), colState.isVisible,
                                                                   true,   // isEnabled
                                                                   false,  // isMouseOver
                                                                   false); // isMouseDown

            g.setColour(getLookAndFeel().findColour(juce::ListBox::textColourId));
            g.setFont(height * 0.7f);
            g.drawText(colState.originalDataColumn->name, checkboxBounds.getRight() + 5, 0, width - (checkboxBounds.getRight() + 10), height,
                       juce::Justification::centredLeft, true);
        }

        void ColumnConfigurationDialogComponent::listBoxItemClicked(int rowNumber, const juce::MouseEvent &event)
        {
            // Toggle visibility if checkbox area is clicked
            juce::Rectangle<int> checkboxBounds(5, (m_columnsListBox.getRowHeight() - 15) / 2, 15, 15);
            if (checkboxBounds.contains(event.x, event.y))
            {
                m_dialogColumnStates[rowNumber].isVisible = !m_dialogColumnStates[rowNumber].isVisible;
                m_columnsListBox.repaintRow(rowNumber); // Repaint to show changed checkmark
            }
            // Update enabled state of Up/Down buttons
            m_moveUpButton.setEnabled(rowNumber > 0);
            m_moveDownButton.setEnabled(rowNumber < getNumRows() - 1 && getNumRows() > 1);
        }

        void ColumnConfigurationDialogComponent::applyChanges()
        {
            m_columnsConfigToModifyRef.clear(); // Clear the existing config sections

            // Iterate m_dialogColumnStates. Only add VISIBLE ones to the config,
            // in their CURRENT order in m_dialogColumnStates.
            for (const auto &colState : m_dialogColumnStates)
            {
                if (colState.isVisible)
                {
                    auto *newConfigSection = m_columnsConfigToModifyRef.addNew();
                    newConfigSection->columnName.set(colState.originalDataColumn->name);
                    newConfigSection->columnWidth.set(colState.currentWidth); // Or get current width from table header if edited there
                }
            }
            // The config is now updated.
            if (m_onConfiguredCallback)
                m_onConfiguredCallback(true); // Signal changes were made
        }

        void ColumnConfigurationDialogComponent::resetToDefaults()
        {
            m_columnsConfigToModifyRef.clear(); // Clear specific config

            // Re-populate internal list with defaults (all available, visible, default order/width)
            m_dialogColumnStates.clear();
            int defaultOrder = 0;
            for (const auto &availableCol : m_allAvailableColumns)
            {
                // Assuming a DataColumn has a defaultVisible flag or all are visible by default
                bool isDefaultVisible = true; // Or availableCol.defaultIsVisible;
                m_dialogColumnStates.emplace_back(&availableCol, isDefaultVisible, availableCol.defaultWidth, defaultOrder++);
            }
            m_columnsListBox.updateContent();
            m_columnsListBox.repaint();
            // User still needs to click OK to save this reset state.
        }

    } // namespace ui
} // namespace jucyaudio
