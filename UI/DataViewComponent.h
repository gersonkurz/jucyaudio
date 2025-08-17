#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/TrackInfo.h>
#include <UI/DynamicColumnManager.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
        using namespace database;
        // Forward declaration
        class MainComponent;

        // Alias for the row action callback function type
        using RowActionCallback = std::function<void(RowIndex_t rowNumber, DataAction action, const juce::Point<int> &screenPosition)>;

        // Drop indicator overlay component
        class DropIndicatorOverlay : public juce::Component
        {
        public:
            DropIndicatorOverlay() { setInterceptsMouseClicks(false, false); }
            
            void setDropPosition(int y)
            {
                m_dropY = y;
                repaint();
            }
            
            void clearDropPosition()
            {
                m_dropY = -1;
                repaint();
            }
            
            void paint(juce::Graphics& g) override
            {
                if (m_dropY >= 0)
                {
                    // Draw a thick line to indicate drop position
                    g.setColour(juce::Colours::orange);
                    g.fillRect(0.0f, float(m_dropY - 2), float(getWidth()), 4.0f);
                    
                    // Draw small triangles at the ends
                    juce::Path triangle;
                    triangle.addTriangle(0.0f, float(m_dropY), 8.0f, float(m_dropY - 4), 8.0f, float(m_dropY + 4));
                    g.fillPath(triangle);
                    
                    triangle.clear();
                    triangle.addTriangle(float(getWidth()), float(m_dropY), float(getWidth() - 8), float(m_dropY - 4), float(getWidth() - 8), float(m_dropY + 4));
                    g.fillPath(triangle);
                }
            }
            
        private:
            int m_dropY{-1};
        };

        // Custom TableListBox that forwards Ctrl+wheel events to parent
        class ScalableTableListBox : public juce::TableListBox,
                                      public juce::DragAndDropTarget
        {
        public:
            ScalableTableListBox(const juce::String& name, juce::TableListBoxModel* model);

            void mouseWheelMove(const juce::MouseEvent &event, const juce::MouseWheelDetails &wheel) override;
            void resized() override;
            
            // DragAndDropTarget overrides
            bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
            void itemDragEnter(const SourceDetails& dragSourceDetails) override;
            void itemDragMove(const SourceDetails& dragSourceDetails) override;
            void itemDragExit(const SourceDetails& dragSourceDetails) override;
            void itemDropped(const SourceDetails& dragSourceDetails) override;
            
            DropIndicatorOverlay& getDropOverlay() { return m_dropOverlay; }
            
        private:
            int m_dropTargetRow{-1};
            bool m_insertAbove{true};
            DropIndicatorOverlay m_dropOverlay;
        };

        class DataViewComponent : public juce::Component, 
                                  public juce::DragAndDropContainer,
                                  private juce::TableListBoxModel, 
                                  private juce::Timer
        {
        public:
            explicit DataViewComponent(MainComponent& mainComponent);
            ~DataViewComponent() override;

            void resized() override;
            void mouseWheelMove(const juce::MouseEvent &event, const juce::MouseWheelDetails &wheel) override;

            void setCurrentNode(INavigationNode *node, bool refresh = false);
            auto getCurrentNode() const
            {
                return m_currentNode;
            }
            void refreshView();

            // Callback for row actions
            RowActionCallback m_onRowActionRequested;

            auto getNumSelectedRows() const
            {
                return m_tableListBox.getNumSelectedRows();
            }

            std::vector<TrackInfo> getSelectedTracks() const; // Returns selected tracks from the table
            std::vector<RowIndex_t> getSelectedRowIndices() const;

            std::vector<ObjectId> getUnderlyingObjectIds(const std::vector<RowIndex_t> &rowIndices) const
            {
                std::vector<ObjectId> result;
                if (m_currentNode)
                {
                    for (const auto rowIndex : rowIndices)
                    {
                        const auto objectId{m_currentNode->getObjectIdForRow(rowIndex)};
                        if (objectId)
                        {
                            result.push_back(objectId);
                        }
                    }
                }
                return result;
            }

            std::vector<ObjectId> getSelectedObjectIds() const
            {
                return getUnderlyingObjectIds(getSelectedRowIndices());
            }
            
            // Handle track reordering from drag & drop
            void handleTrackReorder(int sourceRow, int targetRow);
            void handleTracksReorder(const std::vector<int>& sourceRows, int targetRow);
            
            // Public access methods for media key support
            int getTotalRowCount() { return getNumRows(); }
            void selectSingleRow(int rowIndex) { m_tableListBox.selectRow(rowIndex); }

        private:
            // --- juce::Timer overrides ---
            void timerCallback() override;

            // --- juce::TableListBoxModel overrides ---
            int getNumRows() override;
            void paintRowBackground(juce::Graphics &g, int rowNumber, int width, int height, bool rowIsSelected) override;
            void paintCell(juce::Graphics &g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;

            // Corrected signature for sortOrderChanged: columnId is 1-based.
            void sortOrderChanged(int newSortColumnId, bool isForwards) override;

            int getColumnAutoSizeWidth(int columnId) override; // columnId is 1-based index
            void cellClicked(int rowNumber, int columnId, const juce::MouseEvent &e) override;
            void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent &e) override;
            
            // Drag & drop support
            juce::var getDragSourceDescription(const juce::SparseSet<int>& selectedRows) override;

            void updateTableColumns();

            ScalableTableListBox m_tableListBox;
            MainComponent& m_mainComponent;
            INavigationNode *m_currentNode{nullptr};
            std::vector<DataColumnWithIndex> m_currentDataColumns;

            // Font size management
            float m_fontScale{1.0f};
            static constexpr float MIN_FONT_SCALE = 0.5f;
            static constexpr float MAX_FONT_SCALE = 2.0f;
            static constexpr float FONT_SCALE_STEP = 0.1f;
            void updateFontSize();

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DataViewComponent)
        };

    } // namespace ui
} // namespace jucyaudio