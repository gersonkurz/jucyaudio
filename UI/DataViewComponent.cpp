#include <Database/Includes/INavigationNode.h>
#include <Database/Nodes/MixNode.h>
#include <Database/TrackLibrary.h>
#include <UI/DataViewComponent.h>
#include <UI/MainComponent.h>
#include <Utils/UiUtils.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        using namespace database; // For convenience, we use the database namespace here

        ScalableTableListBox::ScalableTableListBox(const juce::String& name, juce::TableListBoxModel* model)
            : juce::TableListBox(name, model)
        {
            // Add the overlay as a child component
            addAndMakeVisible(m_dropOverlay);
        }

        void ScalableTableListBox::mouseWheelMove(const juce::MouseEvent &event, const juce::MouseWheelDetails &wheel)
        {
            if (event.mods.isCommandDown())
            {
                // Forward to parent for scaling
                if (auto *parent = getParentComponent())
                {
                    parent->mouseWheelMove(event.getEventRelativeTo(parent), wheel);
                }
            }
            else
            {
                // Normal scrolling
                TableListBox::mouseWheelMove(event, wheel);
            }
        }
        
        bool ScalableTableListBox::isInterestedInDragSource(const SourceDetails& dragSourceDetails)
        {
            // Check if this is our mix track drag
            return dragSourceDetails.description.toString().startsWith("MixTrackDrag:");
        }
        
        void ScalableTableListBox::itemDragEnter(const SourceDetails& dragSourceDetails)
        {
            spdlog::info("ScalableTableListBox::itemDragEnter");
            m_dropTargetRow = -1;
            m_dropOverlay.clearDropPosition();
        }
        
        void ScalableTableListBox::itemDragMove(const SourceDetails& dragSourceDetails)
        {
            auto pos = dragSourceDetails.localPosition.toInt();
            auto row = getRowContainingPosition(pos.x, pos.y);
            
            if (row >= 0)
            {
                // Determine if we're in the top or bottom half of the row
                auto rowPos = getRowPosition(row, true);
                auto rowHeight = getRowHeight();
                auto relativeY = pos.y - rowPos.getY();
                m_insertAbove = relativeY < rowHeight / 2;
                
                // Check if the position changed
                bool positionChanged = (row != m_dropTargetRow);
                bool sideChanged = false;
                
                if (row == m_dropTargetRow)
                {
                    // Same row, check if we switched from above to below or vice versa
                    bool newInsertAbove = relativeY < rowHeight / 2;
                    sideChanged = (newInsertAbove != m_insertAbove);
                    m_insertAbove = newInsertAbove;
                }
                
                if (positionChanged || sideChanged)
                {
                    m_dropTargetRow = row;
                    auto y = m_insertAbove ? rowPos.getY() : rowPos.getBottom();
                    m_dropOverlay.setDropPosition(y);
                }
            }
        }
        
        void ScalableTableListBox::itemDragExit(const SourceDetails& dragSourceDetails)
        {
            spdlog::info("ScalableTableListBox::itemDragExit");
            m_dropTargetRow = -1;
            m_dropOverlay.clearDropPosition();
        }
        
        void ScalableTableListBox::itemDropped(const SourceDetails& dragSourceDetails)
        {
            spdlog::info("ScalableTableListBox::itemDropped at row {}", m_dropTargetRow);
            
            if (m_dropTargetRow >= 0)
            {
                // Extract the source row from the drag description
                auto desc = dragSourceDetails.description.toString();
                if (desc.startsWith("MixTrackDrag:"))
                {
                    auto sourceRow = desc.substring(13).getIntValue();
                    auto targetRow = m_dropTargetRow;
                    
                    // Adjust target position based on insert position
                    if (!m_insertAbove)
                    {
                        targetRow++;
                    }
                    
                    // If dropping after the source position, we need to adjust
                    if (targetRow > sourceRow)
                    {
                        targetRow--;
                    }
                    
                    spdlog::info("Dropping row {} at position {}", sourceRow, targetRow);
                    
                    // Notify the parent DataViewComponent about the drop
                    if (auto* dataView = findParentComponentOfClass<DataViewComponent>())
                    {
                        dataView->handleTrackReorder(sourceRow, targetRow);
                    }
                }
            }
            
            m_dropTargetRow = -1;
            m_dropOverlay.clearDropPosition();
        }
        
        void ScalableTableListBox::resized()
        {
            TableListBox::resized();
            m_dropOverlay.setBounds(getLocalBounds());
        }


        DataViewComponent::DataViewComponent(MainComponent &mainComponent)
            : m_tableListBox{juce::String{}, this},
              m_mainComponent{mainComponent}
        {
            addAndMakeVisible(m_tableListBox);
            m_tableListBox.setColour(juce::ListBox::outlineColourId, juce::Colours::grey);
            m_tableListBox.setOutlineThickness(1);
            m_tableListBox.setHeaderHeight(30);
            m_tableListBox.setMultipleSelectionEnabled(true);
            
            // Enable mouse events for drag detection
            m_tableListBox.setInterceptsMouseClicks(true, true);
            m_tableListBox.setMouseClickGrabsKeyboardFocus(true);

            // Table will forward Ctrl+wheel events to us

            updateFontSize();
            // startTimer(2000);
        }

        DataViewComponent::~DataViewComponent()
        {
            // stopTimer();
        }

        void DataViewComponent::timerCallback()
        {
            // This method is called automatically by the JUCE message thread every 2 seconds.
            // if (m_currentNode)
            // {
            //     // If the current node is set, we can refresh the view.
            //     m_currentNode->refreshCache(true);
            //     m_tableListBox.updateContent();
            //     m_tableListBox.repaint();
            // }
        }
        void DataViewComponent::resized()
        {
            m_tableListBox.setBounds(getLocalBounds());
        }

        void DataViewComponent::mouseWheelMove(const juce::MouseEvent &event, const juce::MouseWheelDetails &wheel)
        {
            // Check if Ctrl (or Cmd on Mac) is held
            if (event.mods.isCommandDown())
            {
                // Adjust font scale based on wheel delta
                if (wheel.deltaY > 0)
                {
                    m_fontScale = juce::jmin(m_fontScale + FONT_SCALE_STEP, MAX_FONT_SCALE);
                }
                else if (wheel.deltaY < 0)
                {
                    m_fontScale = juce::jmax(m_fontScale - FONT_SCALE_STEP, MIN_FONT_SCALE);
                }

                spdlog::info("DataView: Font scale changed to {:.1f}x", m_fontScale);

                updateFontSize();
                m_tableListBox.updateContent();
                m_tableListBox.repaint();
            }
            else
            {
                // Pass through to normal scrolling
                Component::mouseWheelMove(event, wheel);
            }
        }

        void DataViewComponent::updateFontSize()
        {
            const int baseRowHeight = 22;
            const int scaledRowHeight = static_cast<int>(baseRowHeight * m_fontScale);
            m_tableListBox.setRowHeight(scaledRowHeight);

            // Also scale the header height
            const int baseHeaderHeight = 30;
            const int scaledHeaderHeight = static_cast<int>(baseHeaderHeight * m_fontScale);
            m_tableListBox.setHeaderHeight(scaledHeaderHeight);
        }
        

        void DataViewComponent::setCurrentNode(INavigationNode *node, bool refresh)
        {
            const auto start{std::chrono::high_resolution_clock::now()};
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
            const auto end{std::chrono::high_resolution_clock::now()};
            const auto duration{std::chrono::duration_cast<std::chrono::milliseconds>(end - start)};
            spdlog::info("DataViewComponent::setCurrentNode took {} ms", duration.count());
        }

        void DataViewComponent::refreshView()
        {
            m_tableListBox.updateContent();
        }

        void DataViewComponent::updateTableColumns()
        {
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

                // Apply saved sort order if available
                const auto savedSortOrder = m_currentNode->getCurrentSortOrder();
                if (!savedSortOrder.empty())
                {
                    // Find the column index for the first sort order
                    const auto &firstSort = savedSortOrder[0];
                    for (size_t i = 0; i < m_currentDataColumns.size(); ++i)
                    {
                        if (m_currentDataColumns[i].column->sqlId == firstSort.columnName)
                        {
                            // Column IDs are 1-based
                            const int columnId = static_cast<int>(i) + 1;
                            // JUCE uses 'isForwards' which means ascending when true
                            const bool isForwards = !firstSort.descending;
                            m_tableListBox.getHeader().setSortColumnId(columnId, isForwards);
                            spdlog::info("Applied saved sort order: column '{}' {} (descending={}, isForwards={})",
                                firstSort.columnName,
                                firstSort.descending ? "descending" : "ascending",
                                firstSort.descending,
                                isForwards);
                            break;
                        }
                    }
                }
            }
            m_tableListBox.getHeader().reSortTable();
        }

        int DataViewComponent::getNumRows()
        {
            const auto start{std::chrono::high_resolution_clock::now()};
            if (!m_currentNode)
            {
                return 0;
            }
            int64_t count = 0;
            if (m_currentNode->getNumberOfRows(count))
            {
                const auto end{std::chrono::high_resolution_clock::now()};
                const auto duration{std::chrono::duration_cast<std::chrono::microseconds>(end - start)};
                if (duration.count() > 100)
                    spdlog::info("DataViewComponent::getNumRows took {} us", duration.count());
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

        std::vector<TrackInfo> DataViewComponent::getSelectedTracks() const
        {
            std::vector<TrackInfo> result;
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

        void DataViewComponent::paintRowBackground(
            juce::Graphics &g, int rowNumber, [[maybe_unused]] int width, [[maybe_unused]] int height, bool rowIsSelected)
        {
            auto &lf = getLookAndFeel();
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
            const auto start{std::chrono::high_resolution_clock::now()};
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
            auto &lf = getLookAndFeel();
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

            // Use the scaled font size
            const float baseFontSize = 14.0f;
            g.setFont(juce::Font{juce::FontOptions{}.withHeight(baseFontSize * m_fontScale)});
            g.drawText(textToDisplay, 2, 0, width - 4, height, justification, true);
            const auto end{std::chrono::high_resolution_clock::now()};
            const auto duration{std::chrono::duration_cast<std::chrono::microseconds>(end - start)};
            // if (duration.count() > 100)
            //     spdlog::info("DataViewComponent::paintCell for row {} took {} us", rowNumber, duration.count());
        }

        void DataViewComponent::sortOrderChanged(int newSortColumnId, bool isForwards)
        {
            const int dataColumnIndex = newSortColumnId - 1;

            if (dataColumnIndex >= 0 && static_cast<size_t>(dataColumnIndex) < m_currentDataColumns.size())
            {
                const auto &columnToSortBy = m_currentDataColumns[dataColumnIndex];
                std::vector<SortOrderInfo> sortOrders;
                // isForwards=true means ascending, descending=false
                sortOrders.push_back({columnToSortBy.column->sqlId, !isForwards});

                if (m_currentNode->setSortOrder(sortOrders))
                {
                    refreshView();
                }
            }
        }

        int DataViewComponent::getColumnAutoSizeWidth(int columnId)
        {
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
            spdlog::info("DataViewComponent::cellClicked - row: {}, column: {}, position: ({}, {})", 
                        rowNumber, columnId, e.position.x, e.position.y);
            
            if (e.mods.isRightButtonDown())
            {
                const auto &availableActions{m_currentNode->getRowActions(static_cast<RowIndex_t>(rowNumber))};
                const auto action{showDataActionPopup(availableActions, m_currentNode, getLastKnownMainViewType())};
                if (action != DataAction::None && m_onRowActionRequested)
                {
                    m_onRowActionRequested(static_cast<RowIndex_t>(rowNumber), action, e.getScreenPosition());
                }
            }
        }

        void DataViewComponent::cellDoubleClicked(int rowNumber, [[maybe_unused]] int columnId, const juce::MouseEvent &e)
        {
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

        juce::var DataViewComponent::getDragSourceDescription(const juce::SparseSet<int>& selectedRows)
        {
            spdlog::info("DataViewComponent::getDragSourceDescription called with {} selected rows", 
                        selectedRows.size());
            
            // Check if we're in the right context for drag & drop
            bool inMixView = m_mainComponent.isTrackEditorInMixView();
            spdlog::info("isTrackEditorInMixView returned: {}", inMixView);
            
            if (!inMixView)
            {
                spdlog::info("Not in mix track editor view, returning empty drag description");
                return {};
            }
            
            // For now, just return a simple description
            if (selectedRows.size() > 0)
            {
                int firstRow = selectedRows[0];
                spdlog::info("Creating drag description for row {}", firstRow);
                return juce::String("MixTrackDrag:") + juce::String(firstRow);
            }
            
            return {};
        }
        
        void DataViewComponent::handleTrackReorder(int sourceRow, int targetRow)
        {
            spdlog::info("DataViewComponent::handleTrackReorder - moving row {} to position {}", sourceRow, targetRow);
            
            // Check if we have a current node and if it's a MixNode
            if (!m_currentNode)
            {
                spdlog::error("No current node set");
                return;
            }
            
            // Try to cast to MixNode
            if (auto* mixNode = dynamic_cast<MixNode*>(m_currentNode))
            {
                // Get the track ID for the source row
                const auto* trackInfo = mixNode->getTrackInfoForRow(sourceRow);
                if (!trackInfo)
                {
                    spdlog::error("Failed to get track info for row {}", sourceRow);
                    return;
                }
                
                // Get the MixProjectLoader and perform the reorder
                auto& mixProjectLoader = const_cast<audio::MixProjectLoader&>(mixNode->getMixProjectLoader());
                
                // Create the track move pair
                std::vector<std::pair<TrackId, int>> trackMoves;
                trackMoves.emplace_back(trackInfo->trackId, targetRow);
                
                if (mixProjectLoader.reorderTracks(trackMoves))
                {
                    spdlog::info("Track reorder successful, saving to database");
                    
                    // Save the changes to the database
                    if (mixProjectLoader.saveMix(theTrackLibrary.getMixManager()))
                    {
                        spdlog::info("Mix saved successfully");
                        
                        // Refresh the view to show the new order
                        m_currentNode->refreshCache(true);
                        refreshView();
                    }
                    else
                    {
                        spdlog::error("Failed to save mix to database");
                    }
                }
                else
                {
                    spdlog::error("Failed to reorder tracks");
                }
            }
            else
            {
                spdlog::error("Current node is not a MixNode");
            }
        }

    } // namespace ui
} // namespace jucyaudio
