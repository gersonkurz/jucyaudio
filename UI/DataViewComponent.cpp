#include <Database/Includes/INavigationNode.h>
#include <UI/DataViewComponent.h>
#include <UI/MainComponent.h>
#include <Utils/UiUtils.h>

namespace jucyaudio
{
    namespace ui
    {
        using namespace database; // For convenience, we use the database namespace here

// A small helper for error logging or assertions if a node is not set
#define CHECK_CURRENT_NODE(returnValue)                                                                                                                        \
    if (!m_currentNode)                                                                                                                                        \
    {                                                                                                                                                          \
        /* juce::Logger::writeToLog("DataViewComponent: m_currentNode is null."); */                                                                           \
        /* JUCE_ASSERT_FALSE; */                                                                                                                               \
        return returnValue;                                                                                                                                    \
    }

#define CHECK_CURRENT_NODE_VOID                                                                                                                                \
    if (!m_currentNode)                                                                                                                                        \
    {                                                                                                                                                          \
        /* juce::Logger::writeToLog("DataViewComponent: m_currentNode is null."); */                                                                           \
        /* JUCE_ASSERT_FALSE; */                                                                                                                               \
        return;                                                                                                                                                \
    }

        DataViewComponent::DataViewComponent(MainComponent &owner)
            : m_ownerMainComponent{owner},
              m_tableListBox{juce::String{}, this}
        {
            addAndMakeVisible(m_tableListBox);
            m_tableListBox.setColour(juce::ListBox::outlineColourId, juce::Colours::grey);
            m_tableListBox.setOutlineThickness(1);
            m_tableListBox.setHeaderHeight(30);
            m_tableListBox.setMultipleSelectionEnabled(true);
        }

        DataViewComponent::~DataViewComponent()
        {
        }

        void DataViewComponent::resized()
        {
            m_tableListBox.setBounds(getLocalBounds());
        }

        void DataViewComponent::setCurrentNode(INavigationNode *node, bool refresh)
        {
            if ((m_currentNode == node) && !refresh)
            {
                return;
            }

            m_currentNode = node;
            m_currentDataColumns.clear();

            if (m_currentNode)
            {
                updateTableColumns();
            }
            else
            {
                m_tableListBox.getHeader().removeAllColumns();
            }

            m_tableListBox.updateContent();
        }

        void DataViewComponent::refreshView()
        {
            m_tableListBox.updateContent();
        }

        void DataViewComponent::updateTableColumns()
        {
            CHECK_CURRENT_NODE_VOID;

            m_tableListBox.getHeader().removeAllColumns();
            if (columns::get(m_currentNode, m_currentDataColumns))
            {
                int columnIdCounter = 1;
                for (const auto &dataColumn : m_currentDataColumns)
                {
                    int width = dataColumn.column->defaultWidth > 0 ? dataColumn.column->defaultWidth : 100;

                    juce::TableHeaderComponent::ColumnPropertyFlags columnFlags = static_cast<juce::TableHeaderComponent::ColumnPropertyFlags>(
                        juce::TableHeaderComponent::visible | juce::TableHeaderComponent::resizable | juce::TableHeaderComponent::sortable);

                    // the columnID here is a) one-based, not zero-based, and b) it really is not what *we* mean with our column index
                    m_tableListBox.getHeader().addColumn(dataColumn.column->name, columnIdCounter++, width, 50, -1, columnFlags);
                }
            }
            m_tableListBox.getHeader().reSortTable();
        }

        int DataViewComponent::getNumRows()
        {
            if (!m_currentNode)
            {
                return 0;
            }
            int64_t count = 0;
            if (m_currentNode->getNumberOfRows(count))
            {
                return static_cast<int>(count);
            }
            return 0;
        }

        std::vector<RowIndex_t> DataViewComponent::getSelectedRowIndices() const
        {
            std::vector<RowIndex_t> result;
            if (m_currentNode)
            {
                const auto selectedRows = m_tableListBox.getSelectedRows();

                for (int i = 0; i < selectedRows.getNumRanges(); ++i)
                {
                    const auto range = selectedRows.getRange(i);
                    for (int row = range.getStart(); row < range.getEnd(); ++row)
                    {
                        result.push_back(static_cast<RowIndex_t>(row));
                    }
                }
            }
            return result;
        }

        std::vector<database::TrackInfo> DataViewComponent::getSelectedTracks() const
        {
            std::vector<database::TrackInfo> result;
            if (m_currentNode)
            {
                const auto selectedRows = m_tableListBox.getSelectedRows();

                for (int i = 0; i < selectedRows.getNumRanges(); ++i)
                {
                    const auto range = selectedRows.getRange(i);
                    for (int row = range.getStart(); row < range.getEnd(); ++row)
                    {
                        const auto pti{m_currentNode->getTrackInfoForRow(static_cast<RowIndex_t>(row))};
                        if (pti)
                        {
                            result.push_back(*pti);
                        }
                    }
                }
            }
            return result;
        }

        std::vector<database::TrackId> DataViewComponent::getSelectedTrackIds() const
        {
            std::vector<database::TrackId> result;
            if (m_currentNode)
            {
                const auto selectedRows = m_tableListBox.getSelectedRows();

                for (int i = 0; i < selectedRows.getNumRanges(); ++i)
                {
                    const auto range = selectedRows.getRange(i);
                    for (int row = range.getStart(); row < range.getEnd(); ++row)
                    {
                        const auto pti{m_currentNode->getTrackInfoForRow(static_cast<RowIndex_t>(row))};
                        if (pti)
                        {
                            result.push_back(pti->trackId);
                        }
                    }
                }
            }
            return result;
        }

        void DataViewComponent::paintRowBackground(juce::Graphics &g, int rowNumber, [[maybe_unused]] int width, [[maybe_unused]] int height,
                                                   bool rowIsSelected)
        {
            auto &lf = juce::LookAndFeel::getDefaultLookAndFeel();
            if (rowIsSelected)
            {
                // TableListBox DOES have selectedItemBackgroundColourId
                g.fillAll(lf.findColour(juce::PopupMenu::highlightedBackgroundColourId));
            }
            else
            {
                // For alternating row colours in TableListBox, we need to define them or use L&F general colors.
                // ListBox itself (as per your pasted header) doesn't have odd/even.
                // Let's use a subtle difference from the main ListBox background for odd rows.
                if (rowNumber % 2)
                {
                    // Example: A slightly darker shade of the ListBox background
                    // This is a common way to do it manually if the component doesn't offer it.
                    juce::Colour alternateRowColour = lf.findColour(juce::ListBox::backgroundColourId).interpolatedWith(juce::Colours::black, 0.03f);
                    g.fillAll(alternateRowColour);
                }
                else
                {
                    // For even rows (or if not doing alternating), use the standard background.
                    g.fillAll(lf.findColour(juce::ListBox::backgroundColourId));
                }
            }
        }

        void DataViewComponent::paintCell(juce::Graphics &g, int rowNumber, int columnId, int width, int height, bool rowIsSelected)
        {
            CHECK_CURRENT_NODE_VOID;

            // columns are 1-based in the TableListBoxModel, so we need to adjust
            const int dataColumnIndex = columnId - 1;

            // it must be a column index in our lookup table
            if (dataColumnIndex < 0 || static_cast<size_t>(dataColumnIndex) >= m_currentDataColumns.size())
            {
                g.setColour(juce::Colours::orange);
                g.drawText("Col?", 2, 0, width - 4, height, juce::Justification::centredLeft, true);
                return;
            }

            // this is important: we match OUR column to the underlying model column
            const auto &columnDef = m_currentDataColumns[dataColumnIndex];
            const auto textToDisplay = m_currentNode->getCellText(rowNumber, columnDef.column->index);
            auto &lf = juce::LookAndFeel::getDefaultLookAndFeel();
            if (rowIsSelected)
            {
                // Use the PopupMenu's highlighted text color for selected items
                g.setColour(lf.findColour(juce::PopupMenu::highlightedTextColourId));
            }
            else
            {
                // Use the ListBox's standard text color for non-selected items
                g.setColour(lf.findColour(juce::ListBox::textColourId));
            }

            juce::Justification justification = juce::Justification::centredLeft;
            // Corrected: ColumnAlignment is now known via 'using' directive
            if (columnDef.column->alignment == ColumnAlignment::Center)
                justification = juce::Justification::centred;
            else if (columnDef.column->alignment == ColumnAlignment::Right)
                justification = juce::Justification::centredRight;

            g.setFont(juce::Font{juce::FontOptions{}.withHeight(static_cast<float>(height) * 0.7f)});
            g.drawText(textToDisplay, 2, 0, width - 4, height, justification, true);
        }

        void DataViewComponent::sortOrderChanged(int newSortColumnId, bool isForwards)
        {
            CHECK_CURRENT_NODE_VOID;
            const int dataColumnIndex = newSortColumnId - 1;

            if (dataColumnIndex >= 0 && static_cast<size_t>(dataColumnIndex) < m_currentDataColumns.size())
            {
                const auto &columnToSortBy = m_currentDataColumns[dataColumnIndex];
                std::vector<database::SortOrderInfo> sortOrders;
                sortOrders.push_back({columnToSortBy.column->sqlId, isForwards});

                if (m_currentNode->setSortOrder(sortOrders))
                {
                    refreshView();
                }
            }
        }

        int DataViewComponent::getColumnAutoSizeWidth(int columnId)
        {
            CHECK_CURRENT_NODE(100);
            const int dataColumnIndex = columnId - 1;
            if (dataColumnIndex < 0 || static_cast<size_t>(dataColumnIndex) >= m_currentDataColumns.size())
            {
                return 100;
            }
            const auto &dataColumn = *(m_currentDataColumns[dataColumnIndex].column);
            return dataColumn.defaultWidth > 0 ? dataColumn.defaultWidth : 100;
        }

        void DataViewComponent::cellClicked(int rowNumber, [[maybe_unused]] int columnId, const juce::MouseEvent &e)
        {
            if (e.mods.isRightButtonDown())
            {
                CHECK_CURRENT_NODE_VOID;

                const auto &availableActions{m_currentNode->getRowActions(static_cast<RowIndex_t>(rowNumber))};
                if (!availableActions.empty())
                {
                    juce::PopupMenu menu;
                    for (size_t i = 0; i < availableActions.size(); ++i)
                    {
                        menu.addItem(static_cast<int>(i + 1), dataActionToString(availableActions[i]));
                    }

                    const int result = menu.show();
                    if (result > 0 && result <= static_cast<int>(availableActions.size()))
                    {
                        if (m_onRowActionRequested)
                        {
                            m_onRowActionRequested(static_cast<RowIndex_t>(rowNumber), availableActions[result - 1], e.getScreenPosition());
                        }
                    }
                }
            }
        }

        void DataViewComponent::cellDoubleClicked(int rowNumber, [[maybe_unused]] int columnId, const juce::MouseEvent &e)
        {
            CHECK_CURRENT_NODE_VOID;
            const auto &availableActions{m_currentNode->getRowActions(static_cast<RowIndex_t>(rowNumber))};
            if (!availableActions.empty())
            {
                DataAction actionToExecute = DataAction::None;
                for (const auto &action : availableActions)
                {
                    if (action == DataAction::Play || action == DataAction::ShowDetails)
                    {
                        actionToExecute = action;
                        if (action == DataAction::Play)
                            break;
                    }
                }
                if (actionToExecute == DataAction::None && !availableActions.empty())
                {
                    actionToExecute = availableActions[0];
                }

                if (actionToExecute != DataAction::None && m_onRowActionRequested)
                {
                    m_onRowActionRequested(static_cast<RowIndex_t>(rowNumber), actionToExecute, e.getScreenPosition());
                }
            }
        }

    } // namespace ui
} // namespace jucyaudio
