#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/INavigationNode.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace jucyaudio
{
    namespace ui
    {
        class MainComponent; // If NavigationPanelComponent needs to call
                             // MainComponent directly (though onNodeSelected
                             // callback is preferred for decoupling)

        class NavigationPanelComponent : public juce::Component
        {
        public:
            // Helper class for TreeView items
            class NavTreeViewItem : public juce::TreeViewItem
            {
            public:
                NavTreeViewItem(database::INavigationNode *node, NavigationPanelComponent &owner);
                ~NavTreeViewItem() override;

                bool mightContainSubItems() override;
                void paintItem(juce::Graphics &g, int width, int height) override;
                void itemOpennessChanged(bool isNowOpen) override;

                // This is the key method for selection changes on THIS item
                void itemSelectionChanged(bool isNowSelected) override;
                void rebuildSubItemsFromModel();

                // itemClicked can be useful if you need to distinguish single
                // click from selection, or handle right-clicks specifically on
                // the item.
                void itemClicked(const juce::MouseEvent &e) override;

                auto getNode() const
                {
                    return m_associatedNode;
                }

            private:
                void buildSubItems();

                database::INavigationNode *m_associatedNode; // Retained
                NavigationPanelComponent &m_ownerPanel;      // To call back when selected
                bool m_subItemsBuilt{false};

                JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NavTreeViewItem)
            };

            explicit NavigationPanelComponent(MainComponent &owner); // Pass MainComponent if needed for other reasons
            ~NavigationPanelComponent() override;

            void resized() override;
            juce::TreeView &getTreeView()
            {
                return m_treeView;
            }
            void setRootNode(database::INavigationNode *rootNode); // rootNode will be retained
            static NavTreeViewItem *findTreeViewItemForNode(juce::TreeViewItem *currentItem, database::INavigationNode *targetNode);
            NavTreeViewItem *findTreeViewItemForNode(database::INavigationNode *targetNode);
            void removeNodeFromTree(database::INavigationNode *nodeToRemove); // nodeToRemove will be removed from the
                                                                              // tree, but not deleted
            void refreshNode(database::INavigationNode *node);                // rootNode will be retained
            void selectNode(database::INavigationNode *nodeToSelect);         // Selects a node in the
                                                                              // TreeView, retaining it

            // Callback for when a node is selected in the TreeView
            std::function<void(database::INavigationNode *)> m_onNodeSelected;

            // Called by NavTreeViewItem when it is selected
            void handleItemSelection(NavTreeViewItem *selectedItem);

            // Callback for when a node is selected in the TreeView
            std::function<void(database::INavigationNode *, database::DataAction)> m_onNodeAction;

        private:
            juce::TreeView m_treeView;
            database::INavigationNode *m_currentRootNode{nullptr}; // Retained

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NavigationPanelComponent)
        };
    } // namespace ui
} // namespace jucyaudio
