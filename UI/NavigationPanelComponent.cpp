#include <Database/Includes/INavigationNode.h>
#include <Database/Nodes/RootNode.h>
#include <UI/NavigationPanelComponent.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {

        NavigationPanelComponent::NavTreeViewItem::NavTreeViewItem(database::INavigationNode *node, NavigationPanelComponent &owner)
            : m_associatedNode{node},
              m_ownerPanel{owner}
        {

            if (m_associatedNode)
            {
                m_associatedNode->retain(REFCOUNT_DEBUG_ARGS);
            }
        }

        NavigationPanelComponent::NavTreeViewItem::~NavTreeViewItem()
        {
            // Release the node when the TreeViewItem is destroyed
            if (m_associatedNode)
            {
                m_associatedNode->release(REFCOUNT_DEBUG_ARGS);
                m_associatedNode = nullptr;
            }
        }

        bool NavigationPanelComponent::NavTreeViewItem::mightContainSubItems()
        {
            if (m_associatedNode)
            {
                return m_associatedNode->hasChildren();
            }
            return false;
        }

        void NavigationPanelComponent::NavTreeViewItem::paintItem(juce::Graphics &g, int width, int height)
        {
            // Get the LookAndFeel for color lookups
            auto &lf = getOwnerView()->getLookAndFeel();

            if (isSelected())
            {
                // Use the TreeView's specific selected item background color
                g.fillAll(lf.findColour(juce::TreeView::selectedItemBackgroundColourId));
                // For selected text, there isn't a TreeView-specific one.
                // We can use a general Label text color, or a contrasting
                // color. Let's use the default Label text color, assuming the
                // selected background provides enough contrast. For better
                // results, a L&F could define a specific "selected tree item
                // text" color.
                g.setColour(lf.findColour(juce::Label::textColourId));
            }
            else
            {
                // Use the TreeView's general background color for non-selected
                // items. Alternatively, you could use
                // oddItemsColourId/evenItemsColourId if you set them up.
                g.fillAll(getOwnerView()->findColour(juce::TreeView::backgroundColourId));
                g.setColour(lf.findColour(juce::Label::textColourId)); // General text color for
                                                                       // non-selected items
            }

            if (m_associatedNode)
            {
                // Corrected font usage
                g.setFont(juce::Font{juce::FontOptions{}.withHeight(height * 0.7f)});
                g.drawText(m_associatedNode->getName(), 4, 0, width - 6,
                           height, // Leave a small margin
                           juce::Justification::centredLeft, true);
            }
            else
            {
                g.setFont(juce::Font{juce::FontOptions{}.withHeight(height * 0.7f)});
                g.drawText("Error: No Node", 4, 0, width - 6, height, juce::Justification::centredLeft, true);
            }
        }

        void NavigationPanelComponent::NavTreeViewItem::itemOpennessChanged(bool isNowOpen)
        {
            if (isNowOpen && !m_subItemsBuilt)
            {
                buildSubItems();
            }
            else if (!isNowOpen)
            {
                // TreeViewItem's clearSubItems() will handle destroying
                // children. If we had custom logic for when an item is closed
                // (e.g., releasing resources specifically loaded by
                // buildSubItems beyond just the child nodes themselves), it
                // would go here. For now, the automatic destruction of child
                // NavTreeViewItems (which releases their nodes) is sufficient.
                // We could set subItemsBuilt = false here if we want them
                // rebuilt on next open, but clearSubItems() effectively does
                // this.
            }
        }

        void NavigationPanelComponent::NavTreeViewItem::itemSelectionChanged(bool isNowSelected)
        {
            // This method is called by the TreeView when this item's selection
            // state changes.
            if (isNowSelected)
            {
                // Notify the parent panel that this item has been selected.
                // The ownerPanel will then use its onNodeSelected callback.
                m_ownerPanel.handleItemSelection(this);
            }
            // If !isNowSelected, we could inform the ownerPanel of deselection
            // if needed, but typically selection of another item implicitly
            // deselects this one. The TreeView itself handles ensuring only one
            // item is selected (by default).
        }

        void NavigationPanelComponent::NavTreeViewItem::buildSubItems()
        {
            // Ensure we don't rebuild if already built (unless explicitly
            // cleared)
            if (m_subItemsBuilt || !m_associatedNode)
            {
                return;
            }

            clearSubItems(); // Clear any existing (shouldn't be any if
                             // subItemsBuilt is false, but good practice)

            std::vector<database::INavigationNode *> children;
            if (m_associatedNode->getChildren(children))
            {
                for (const auto childNode : children)
                {
                    if (childNode) // childNode is already retained by
                                   // getChildren()
                    {
                        // The NavTreeViewItem constructor will take its own
                        // retain() on childNode. The childNode pointer from the
                        // 'children' vector can then be released.
                        addSubItem(new NavTreeViewItem{childNode, m_ownerPanel});
                    }
                }
            }
            // If getChildren failed or returned an empty vector, no sub-items
            // will be added. Release any remaining nodes in the children vector
            // if getChildren populated it but we didn't use all. However, the
            // loop above processes all non-null children. If getChildren
            // allocated nodes but returned false, it's its responsibility to
            // clean them up. Our contract is that if getChildren returns true,
            // 'children' contains retained nodes.
            for (auto ptr : children)
            {
                if (ptr)
                {
                    ptr->release(REFCOUNT_DEBUG_ARGS); // Release each pointer
                }
            }
            m_subItemsBuilt = true;
        }

        //==============================================================================
        // NavigationPanelComponent Implementation
        //==============================================================================

        NavigationPanelComponent::NavigationPanelComponent(MainComponent & /*owner*/) // ownerMainComponent parameter removed as it's not
                                                                                      // used directly : ownerMainComponent(owner) // No
                                                                                      // longer storing ownerMainComponent if only for
                                                                                      // callbacks
        {
            addAndMakeVisible(m_treeView);
            m_treeView.setIndentSize(20); // Example indent size
            // We don't add this as a TreeView::Listener anymore.
            // Selection is handled via NavTreeViewItem::itemSelectionChanged ->
            // handleItemSelection.
        }

        NavigationPanelComponent::~NavigationPanelComponent()
        {
            if (m_treeView.getRootItem())
            {
                m_treeView.deleteRootItem(); // Juce 7/8 might have this
                // Or more manually if deleteRootItem() isn't public or what we
                // need: delete root; // This is DANGEROUS if TreeView still
                // thinks it owns it. A safer manual clear:
                // root->clearSubItems(); // Delete children
                // delete root; // Then delete root
                // No, this is too risky. Let's not do this manually unless as a
                // last resort.
            }
            // The TreeView will destroy its root item, which will cascade
            // destruction to all NavTreeViewItems, causing their associated
            // INavigationNodes to be released. We also need to release our
            // reference to the currentRootNode.
            m_treeView.setRootItem(nullptr); // This ensures all NavTreeViewItems are destroyed and
                                             // nodes released.
            if (m_currentRootNode)
            {
                m_currentRootNode->release(REFCOUNT_DEBUG_ARGS);
                m_currentRootNode = nullptr;
            }
        }

        void NavigationPanelComponent::resized()
        {
            m_treeView.setBounds(getLocalBounds());
        }

        void NavigationPanelComponent::NavTreeViewItem::itemClicked(const juce::MouseEvent &e)
        {
            // First, ensure the item is selected when clicked (if that's the
            // desired behavior for any click) TreeView might do this
            // automatically, but explicit selection can be useful.
            // setSelected(true, true); // Select this item, and notify
            // listeners
            if (e.mods.isRightButtonDown())
            {
                // Right-click detected!
                // Now we need to get the available actions for this node and
                // show the menu.

                if (m_associatedNode == nullptr)
                {
                    return; // Should not happen if node is valid
                }

                const auto &availableActions{m_associatedNode->getNodeActions()};
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
                        if (m_ownerPanel.m_onNodeAction)
                        {
                            m_ownerPanel.m_onNodeAction(m_associatedNode, availableActions[result - 1]);
                        }
                    }
                }
            }
            else
            {
                // Handle left-click if needed (e.g., if itemSelectionChanged
                // isn't sufficient or if you want single click to also trigger
                // selection explicitly). Often, TreeView's default behavior for
                // selection is fine, and itemSelectionChanged handles the
                // aftermath. You might call TreeViewItem::itemClicked(e) if you
                // want default behavior for other clicks.
            }
        }

        void NavigationPanelComponent::setRootNode(database::INavigationNode *rootNode)
        {
            // Release any existing root node this panel might be holding a
            // reference to
            if (m_currentRootNode)
            {
                m_currentRootNode->release(REFCOUNT_DEBUG_ARGS);
                m_currentRootNode = nullptr;
            }

            m_currentRootNode = rootNode; // The incoming rootNode is assumed to be already
                                          // retained by the caller (MainComponent)

            if (m_currentRootNode)
            {
                m_currentRootNode->retain(REFCOUNT_DEBUG_ARGS);
                // currentRootNode is already retained by MainComponent.
                // NavTreeViewItem will take its own retain. So, we don't need
                // an extra retain here for currentRootNode itself, but the new
                // NavTreeViewItem will retain it.
                const auto rootItem = new NavTreeViewItem{m_currentRootNode, *this};
                m_treeView.setRootItem(rootItem);

                // Configure TreeView to hide root item decorations
                m_treeView.setRootItemVisible(false); // This is the key JUCE method!

                // Ensure root is open so children are visible
                rootItem->setOpen(true);
            }
            else
            {
                m_treeView.setRootItem(nullptr);
            }
        }

        void NavigationPanelComponent::handleItemSelection(NavTreeViewItem *selectedItem)
        {
            if (m_onNodeSelected)
            {
                if (selectedItem)
                {
                    database::INavigationNode *node = selectedItem->getNode();
                    if (node)
                    {
                        // The node is already retained by the NavTreeViewItem.
                        // The onNodeSelected callback expects a retained node
                        // that the receiver (MainComponent) will become
                        // responsible for releasing. So, we retain it here
                        // before passing.
                        node->retain(REFCOUNT_DEBUG_ARGS);
                        m_onNodeSelected(node);
                    }
                    else
                    {
                        // This case (valid selectedItem but null node) should
                        // ideally be prevented by assertions in
                        // NavTreeViewItem's constructor.
                        m_onNodeSelected(nullptr);
                    }
                }
                else
                {
                    // This might be called if we explicitly want to signal "no
                    // selection". For instance, if the tree view itself clears
                    // selection.
                    m_onNodeSelected(nullptr);
                }
            }
        }

        void NavigationPanelComponent::selectNode(database::INavigationNode *nodeToSelect)
        {
            if (nodeToSelect)
            {
                spdlog::info("MainComponent: Attempting to select node '{}' in "
                             "navigation.",
                             nodeToSelect->getName());

                // Find the GUI item for this specific model node
                const auto treeItemToSelect = findTreeViewItemForNode(nodeToSelect);

                if (treeItemToSelect)
                {
                    // Ensure all parents are open so the item *can* be made
                    // visible
                    juce::TreeViewItem *currentParent =
                        treeItemToSelect->getParentItem();
                    while (currentParent != nullptr &&
                           currentParent != getTreeView().getRootItem())
                    {
                        // Don't try to open the hidden root item if
                        // getRootItemVisible() is false
                        if (!currentParent->isOpen())
                        {
                            currentParent->setOpen(true);
                        }
                        currentParent = currentParent->getParentItem();
                    }

                    // Now select the item. If
                    // m_treeView.setScrollToShowItemsThatAreSelected(true) was
                    // called, JUCE should handle the scrolling.
                    treeItemToSelect->setSelected(true, true);
                    getTreeView().scrollToKeepItemVisible(treeItemToSelect);

                    // No direct scrollToItem needed if the above property is
                    // set. TreeView will handle it on selection.
                    spdlog::info("MainComponent: Successfully selected new "
                                 "item. TreeView should scroll if configured.");
                }
                else
                {
                    spdlog::warn(
                        "MainComponent: Could not find the NavTreeViewItem for "
                        "the newly created model node.");
                    // ... (fallback logic) ...
                }
            }
        }

        NavigationPanelComponent::NavTreeViewItem *NavigationPanelComponent::findTreeViewItemForNode(database::INavigationNode *targetNode)
        {
            return findTreeViewItemForNode(m_treeView.getRootItem(), targetNode);
        }

        NavigationPanelComponent::NavTreeViewItem *NavigationPanelComponent::findTreeViewItemForNode(juce::TreeViewItem *currentItem,
                                                                                                     database::INavigationNode *targetNode)
        {
            if (currentItem == nullptr)
            {
                return nullptr;
            }

            auto *navItem = dynamic_cast<NavigationPanelComponent::NavTreeViewItem *>(currentItem);
            if (navItem && navItem->getNode() == targetNode)
            {
                return navItem;
            }

            for (int i = 0; i < currentItem->getNumSubItems(); ++i)
            {
                if (auto *found = findTreeViewItemForNode(currentItem->getSubItem(i), targetNode))
                {
                    return found;
                }
            }
            return nullptr;
        }

        void NavigationPanelComponent::refreshNode(database::INavigationNode *nodeToRefresh)
        {
            if (!m_currentRootNode || !m_treeView.getRootItem())
            {
                return;
            }

            if (!nodeToRefresh)
            {
                return;
            }
            nodeToRefresh->refreshChildren();
            nodeToRefresh->release(REFCOUNT_DEBUG_ARGS); // Release the old

            // Find the TreeViewItem associated with the nodeToRemove
            const auto treeViewItemToRefresh = findTreeViewItemForNode(nodeToRefresh);
            if (!treeViewItemToRefresh)
            {
                return;
            }
            // 5. Trigger the GUI update for this TreeViewItem's children
            const std::string strDisplayNode{treeViewItemToRefresh->getNode() ? treeViewItemToRefresh->getNode()->getName() : "UNKNOWN_NODE_IN_GUI_ITEM"};
            spdlog::info("NavigationPanel::refreshNode - Refreshing GUI sub-items for "
                         "TreeViewItem displaying node: {}",
                         strDisplayNode);

            // To force a rebuild of its GUI children based on the now-updated
            // model: a. Mark its current GUI children as not built (if they
            // were). b. If it's open, this should trigger itemOpennessChanged
            // -> buildSubItems. c. If it's closed, opening it later will
            // trigger buildSubItems.

            // Simplest way to force NavTreeViewItem::buildSubItems() to run
            // again:
            treeViewItemToRefresh->clearSubItems(); // Removes all existing GUI child items from
                                                    // this item and calls their destructors
                                                    // (which releases their nodes). This is
                                                    // important for refcounting.
            // treeViewItemToRefresh->m_subItemsBuilt = false; // Manually reset
            // the flag.
            // ^^^^ This direct member access is bad. NavTreeViewItem needs a
            // method.

            // Add a method to NavTreeViewItem to allow external invalidation of
            // its children build state: void
            // NavTreeViewItem::invalidateSubItems() {
            //     clearSubItems(); // This already removes and destroys GUI
            //     children m_subItemsBuilt = false;
            // }
            // Then call:
            // treeViewItemToRefresh->invalidateSubItems();

            // Let's assume NavTreeViewItem has a public method:
            treeViewItemToRefresh->rebuildSubItemsFromModel(); // You will create this method

            // If the item was open, it should now re-populate based on the
            // fresh model children. If it wasn't open, it will build them when
            // next opened. To ensure it visibly updates if it was already open:
            if (treeViewItemToRefresh->isOpen())
            {
                // JUCE's TreeView might not automatically re-query children
                // just because clearSubItems() was called if no explicit signal
                // is sent to the TreeView itself about the structure changing
                // for an *already open item*. Forcing it open again after
                // clearing often works, or telling the TreeView item height
                // changed. treeViewItemToRefresh->setOpen(false); // Close it
                // treeViewItemToRefresh->setOpen(true);  // And reopen it to
                // trigger buildSubItems via itemOpennessChanged This causes a
                // visual flicker.

                // A cleaner way might be needed if just calling a rebuild
                // method doesn't refresh an open tree. For now, let's assume
                // rebuildSubItemsFromModel() does what's needed.
            }

            m_treeView.repaint(); // Ensure the tree view repaints
        }

        void NavigationPanelComponent::NavTreeViewItem::rebuildSubItemsFromModel()
        {
            // This method explicitly clears existing GUI children and rebuilds
            // them from the current state of m_associatedNode->m_children. It
            // assumes m_associatedNode->refreshChildren() (the model update)
            // has already been called.

            spdlog::debug("NavTreeViewItem (Node: {}): Rebuilding sub-items from model.", m_associatedNode ? m_associatedNode->getName() : "null");

            // 1. Clear existing GUI sub-items. This is crucial.
            //    clearSubItems() destroys the child NavTreeViewItem objects,
            //    which in turn release their associated INavigationNode models.
            clearSubItems();

            // 2. Reset the flag so buildSubItems knows to run.
            m_subItemsBuilt = false;

            // 3. Call buildSubItems to repopulate with new GUI items based on
            // the (already refreshed) model.
            //    This will call m_associatedNode->getChildren() which should
            //    return the updated list of child models from
            //    nodeToRefreshModel->m_children.
            buildSubItems();

            // 4. If the item is open, the TreeView should now reflect the new
            // children.
            //    If TreeView needs an extra hint:
            //    TreeView* owner = getOwnerView();
            //    if (owner) owner->repaint(); // Or owner->updateContent() if
            //    that's relevant for TreeView
        }

        void NavigationPanelComponent::removeNodeFromTree(database::INavigationNode *nodeToRemove)
        {
            if (nodeToRemove == nullptr || m_treeView.getRootItem() == nullptr)
            {
                return;
            }

            // Find the TreeViewItem associated with the nodeToRemove
            NavTreeViewItem *itemToRemove = findTreeViewItemForNode(nodeToRemove);

            if (itemToRemove)
            {
                // Get its parent TreeViewItem
                juce::TreeViewItem *parentItem = itemToRemove->getParentItem();
                if (parentItem)
                {
                    // Find the index of the item to remove within its parent
                    int indexInParent = -1;
                    for (int i = 0; i < parentItem->getNumSubItems(); ++i)
                    {
                        if (parentItem->getSubItem(i) == itemToRemove)
                        {
                            indexInParent = i;
                            break;
                        }
                    }

                    if (indexInParent != -1)
                    {
                        // Remove the sub-item from its parent.
                        // This will delete the itemToRemove (as TreeViewItems
                        // own their sub-items) and trigger appropriate UI
                        // updates.
                        parentItem->removeSubItem(indexInParent,
                                                  true); // true to delete the item
                                                         // The TreeView should refresh automatically.
                    }
                    else
                    {
                        spdlog::warn("MTE: Could not find index of item to "
                                     "remove in its parent.");
                        // Fallback: brute-force refresh if specific removal
                        // fails
                        m_treeView.getRootItem()->clearSubItems(); // This deletes all sub-items
                        // Then rebuild. This is less ideal than targeted
                        // removal. If m_currentRootNode is your model's root:
                        // NavTreeViewItem* newRootGuiItem = new
                        // NavTreeViewItem(m_currentRootNode, *this);
                        // m_treeView.setRootItem(newRootGuiItem); // This takes
                        // ownership m_treeView.setRootItemVisible(false); // Or
                        // true, depending on your setup A simpler full refresh
                        // if you have setRootNode:
                        // setRootNode(m_currentRootNode); // If this rebuilds
                        // the tree
                        spdlog::warn("MTE: Consider a more robust tree refresh method "
                                     "if targeted removal fails frequently.");
                        m_treeView.repaint(); // Just in case
                    }
                }
                else if (m_treeView.getRootItem() == itemToRemove) // Trying to remove the root item itself
                {
                    // This case is trickier. You probably don't want to remove
                    // the GUI root item if it represents your INavigationNode
                    // root. Or if it does, you set it to nullptr.
                    m_treeView.setRootItem(nullptr); // This deletes the old root and its children
                    m_currentRootNode = nullptr;     // Assuming m_currentRootNode
                                                     // is your model root
                    spdlog::info("MTE: Root TreeViewItem removed.");
                }
                else
                {
                    spdlog::warn("MTE: Item to remove has no parent in the "
                                 "TreeView, but is not root. This is unusual.");
                    // Fallback refresh
                    m_treeView.repaint();
                }
            }
            else
            {
                spdlog::warn("MTE: Could not find TreeViewItem to remove for "
                             "INavigationNode (ptr: {}). Tree might be out of sync.",
                             (void *)nodeToRemove);
                // Fallback refresh
                m_treeView.repaint();
            }
        }

    } // namespace ui
} // namespace jucyaudio