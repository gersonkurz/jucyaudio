#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/TrackInfo.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <UI/DynamicColumnManager.h>

namespace jucyaudio
{
    namespace ui
    {
        // Forward declaration
        class MainComponent;
        // Using directives for convenience within this UI namespace
        using jucyaudio::database::DataAction;
        using database::DataColumn; // For m_currentDataColumns
        using database::INavigationNode; // For m_currentNode

        // Alias for the row action callback function type
        using RowActionCallback = std::function<void(RowIndex_t rowNumber, DataAction action, const juce::Point<int> &screenPosition)>;

        // Custom TableListBox that forwards Ctrl+wheel events to parent
        class ScalableTableListBox : public juce::TableListBox
        {
        public:
            using juce::TableListBox::TableListBox;
            
            void mouseWheelMove(const juce::MouseEvent &event, const juce::MouseWheelDetails &wheel) override
            {
                if (event.mods.isCommandDown())
                {
                    // Forward to parent for scaling
                    if (auto* parent = getParentComponent())
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
        };
        
        class DataViewComponent : public juce::Component, private juce::TableListBoxModel, private juce::Timer
        {
        public:
            explicit DataViewComponent();
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

            std::vector<database::TrackInfo> getSelectedTracks() const; // Returns selected tracks from the table
            std::vector<RowIndex_t> getSelectedRowIndices() const; 

            template <typename T> std::vector<T> getUnderlyingObjectIds(const std::vector<RowIndex_t>& rowIndices) const
            {
                std::vector<T> result;
                if (m_currentNode)
                {
                    for (const auto rowIndex : rowIndices)
                    {
                        const auto objectId{m_currentNode->getObjectIdForRow(rowIndex)};
                        if (objectId)
                        {
                            result.push_back(static_cast<T>(objectId));
                        }
                    }
                }
                return result;
            }

            template <typename T> std::vector<T> getUnderlyingObjectIds() const
            {
                return getUnderlyingObjectIds<T>(getSelectedRowIndices());
            }

            std::vector<database::TrackId> getSelectedTrackIds() const
            {
                return getUnderlyingObjectIds<database::TrackId>();
            }
            
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

            void updateTableColumns();

            ScalableTableListBox m_tableListBox;
            INavigationNode *m_currentNode{nullptr};
            std::vector<database::DataColumnWithIndex> m_currentDataColumns;
            
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