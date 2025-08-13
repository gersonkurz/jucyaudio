#pragma once

#include <Database/Includes/Constants.h>

#include <Database/Includes/INavigationNode.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace jucyaudio
{
    namespace ui
    {
        using namespace database;

        // forward declarations
        class NavigationPanelComponent;
        class MainComponent; 

        class NavTreeViewItem final : public juce::TreeViewItem
        {
        public:
            NavTreeViewItem(INavigationNode *node, NavigationPanelComponent &owner);
            ~NavTreeViewItem() override;

            // @brief This method is called by the TreeView to determine if this item
            // might have sub-items. It should return true if the item can expand.
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
                return m_node;
            }

        private:
            // @brief This is our own method to build sub-items from the node's children.
            bool buildSubItems();

            INavigationNode *m_node;      // Retained
            NavigationPanelComponent &m_ownerPanel; // To call back when selected
            // bool m_subItemsBuilt{false}; // not sure this optimization is needed 

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NavTreeViewItem)
        };

        class NavigationPanelComponent : public juce::Component
        {
        public:

            explicit NavigationPanelComponent(MainComponent &owner); // Pass MainComponent if needed for other reasons
            ~NavigationPanelComponent() override;

            void resized() override;
            juce::TreeView &getTreeView()
            {
                return m_treeView;
            }
            static NavTreeViewItem *findTreeViewItemForNode(juce::TreeViewItem *currentItem, INavigationNode *targetNode);
            NavTreeViewItem *findTreeViewItemForNode(INavigationNode *targetNode);
            void removeNodeFromTree(INavigationNode *nodeToRemove); // nodeToRemove will be removed from the
                                                                              // tree, but not deleted
            void refreshNode(INavigationNode *node);                // rootNode will be retained
            void selectNode(const INavigationNode *nodeToSelect);         // Selects a node in the
            void expand(INavigationNode *node);
            bool expandPathAndSelectTarget(const std::vector<INavigationNode*>& pathFromRoot);

            // Callback for when a node is selected in the TreeView
            std::function<void(INavigationNode *)> m_onNodeSelected;

            // Called by NavTreeViewItem when it is selected
            void handleItemSelection(NavTreeViewItem *selectedItem);

            // Callback for when a node is selected in the TreeView
            std::function<void(INavigationNode *, DataAction)> m_onNodeAction;

            // ---------------------------- new UI-related methods

            // @brief Set the root node for this navigation panel and create the initial display
            // @param rootNode The root node to set. This node will be retained.
            // @return True if the root node was set successfully, false if a root node is already set.
            bool setRootNode(INavigationNode *rootNode);

            void releaseRootNode();

        private:
            juce::TreeView m_treeView;
            INavigationNode *m_currentRootNode{nullptr}; // Retained

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NavigationPanelComponent)
        };

    } // namespace ui
} // namespace jucyaudio
