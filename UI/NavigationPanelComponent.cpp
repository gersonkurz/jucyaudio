#include <Database/Includes/INavigationNode.h>
#include <Database/Nodes/RootNode.h>
#include <UI/CustomColourIds.h>
#include <UI/MainComponent.h>
#include <UI/NavigationPanelComponent.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        using namespace database;

        NavTreeViewItem::NavTreeViewItem(INavigationNode *node, NavigationPanelComponent &owner)
            : m_node{node},
              m_ownerPanel{owner}
        {
            assert(node != nullptr); // Ensure we have a valid node
            if (node)
            {
                node->retain(REFCOUNT_DEBUG_ARGS);
            }
        }

        NavTreeViewItem::~NavTreeViewItem()
        {
            // Release the node when the TreeViewItem is destroyed
            if (m_node)
            {
                m_node->release(REFCOUNT_DEBUG_ARGS);
                m_node = nullptr;
            }
        }

        bool NavTreeViewItem::mightContainSubItems()
        {
            if (m_node)
            {
                return m_node->canExpand();
            }
            return false;
        }

        void NavTreeViewItem::paintItem(juce::Graphics &g, int width, int height)
        {
            // Use the width and height parameters to define the local bounds,
            // which sidesteps the strange compiler error with getLocalBounds().
            const juce::Rectangle<int> localBounds{0, 0, width, height};
            const auto textBounds = localBounds.reduced(4, 2);
            auto &lf = getOwnerView()->getLookAndFeel();

            // Check if this node represents an offline folder
            bool isNodeOnline = true;
            if (m_node)
            {
                isNodeOnline = m_node->isOnline();
            }

            if (isSelected())
            {
                const bool treeHasFocus = getOwnerView()->hasKeyboardFocus(true);

                const auto highlightColour = treeHasFocus ? lf.findColour(juce::PopupMenu::highlightedBackgroundColourId)
                                                          : lf.findColour(juce::ComboBox::backgroundColourId); // Inactive selection color

                g.setColour(highlightColour);
                g.fillRect(localBounds);

                // Use a contrasting text color.
                const auto textColor = lf.findColour(juce::PopupMenu::highlightedTextColourId);
                g.setColour(textColor);
                //spdlog::debug("NavTreeViewItem::paintItem - Selected text color: #{}", textColor.toDisplayString(true).toStdString());
            }
            else
            {
                const auto backgroundColour = lf.findColour(juce::TreeView::backgroundColourId);
                g.setColour(backgroundColour);
                g.fillRect(localBounds);

                // Use different text color based on online/offline status
                const auto foregroundColour = isNodeOnline 
                    ? lf.findColour(ui::folderOnlineTextColourId)
                    : lf.findColour(ui::folderOfflineTextColourId);
                g.setColour(foregroundColour);
                //spdlog::debug("NavTreeViewItem::paintItem - Non-selected text color: #{}", foregroundColour.toDisplayString(true).toStdString());
            }

            if (m_node)
            {
                // Use the modern, non-deprecated Font constructor that you were already using.
                g.setFont(juce::Font{juce::FontOptions{}.withHeight(height * 0.7f)});
                g.drawText(getSafeDisplayText(m_node->getName()), textBounds, juce::Justification::centredLeft, true);
            }
            else
            {
                g.setFont(juce::Font{juce::FontOptions{}.withHeight(height * 0.7f)});
                g.setColour(juce::Colours::red);
                g.drawText("Error: No Node", textBounds, juce::Justification::centredLeft, true);
            }
        }

        void NavTreeViewItem::itemOpennessChanged(bool isNowOpen)
        {
            //if (isNowOpen && !m_subItemsBuilt)
            if (isNowOpen)
            {
                buildSubItems();
            }
            else 
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

        void NavTreeViewItem::itemSelectionChanged(bool isNowSelected)
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

        bool NavTreeViewItem::buildSubItems()
        {
            if (!m_node)
            {
                spdlog::error("buildSubItems: Node is null, cannot build sub-items.");
                return false;
            }

            //if (m_subItemsBuilt)
            //{
            //    spdlog::warn("buildSubItems: Sub-items already built, skipping.");
            //    return true; // Already built, nothing to do
            // }

            clearSubItems(); // Clear any existing (shouldn't be any if subItemsBuilt is false, but good practice)
            if (m_node->canExpand())
            {
                std::vector<INavigationNode *> nodes;
                if (m_node->expand(nodes))
                {
                    for (auto &node : nodes)
                    {
                        // The NavTreeViewItem constructor will take its own
                        // retain() on childNode. The childNode pointer from the
                        // 'children' vector can then be released.
                        addSubItem(new NavTreeViewItem{node, m_ownerPanel});
                        node->release(REFCOUNT_DEBUG_ARGS); // Release the original node pointer
                    }
                }
                spdlog::info("buildSubItems: built a total of {} sub-items for node '{}'.", nodes.size(), m_node->getName());
            }
            else
            {
                spdlog::info("buildSubItems: Node '{}' cannot expand, no sub-items built.", m_node->getName());
            }
            //m_subItemsBuilt = true;
            return true;
        }

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

        void NavTreeViewItem::itemClicked(const juce::MouseEvent &e)
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

                if (m_node == nullptr)
                {
                    return; // Should not happen if node is valid
                }

                const auto &availableActions{m_node->getNodeActions()};
                const auto action{showDataActionPopup(availableActions, m_node, getLastKnownMainViewType())};
                if (action != DataAction::None && m_ownerPanel.m_onNodeAction)
                {
                    m_ownerPanel.m_onNodeAction(m_node, action);
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
        
        void NavigationPanelComponent::releaseRootNode()
        {
            m_treeView.deleteRootItem(); // This will delete the root item and all its children
            if (m_currentRootNode)
            {
                m_currentRootNode->release(REFCOUNT_DEBUG_ARGS);
                m_currentRootNode = nullptr;
            }
        }

        bool NavigationPanelComponent::setRootNode(INavigationNode *rootNode)
        {
            if (!rootNode && m_currentRootNode)
            {
                m_currentRootNode->release(REFCOUNT_DEBUG_ARGS);
                m_currentRootNode = nullptr;
                return true;
            }
            if (m_currentRootNode != nullptr)
            {
                spdlog::error("You cannot set a new root node in NavigationPanelComponent while one is already set. Please clear the current root node first.");
                return false;
            }
            if (!rootNode)
            {
                spdlog::error("You cannot set a null root node in NavigationPanelComponent.");
                return false;
            }

            m_currentRootNode = rootNode;
            m_currentRootNode->retain(REFCOUNT_DEBUG_ARGS);

            const auto rootItem = new NavTreeViewItem{m_currentRootNode, *this};
            m_treeView.setRootItem(rootItem);

            // Configure TreeView to hide root item decorations
            m_treeView.setRootItemVisible(false);

            // Ensure root is open so children are visible
            rootItem->setOpen(true);

            // TODO: select and display the first child of root
            /*
            if (getTreeView().getNumRowsInTree() > 0) // Check if TreeView has any visible items
            {
                if (auto *firstTopLevelTreeViewItem =
                        dynamic_cast<NavTreeViewItem *>(m_navigationPanel.getTreeView().getItemOnRow(0)))
                {
                        firstTopLevelTreeViewItem->setSelected(true, true);
                    }
                }
            */
            return true;
        }

        void NavigationPanelComponent::handleItemSelection(NavTreeViewItem *selectedItem)
        {
            if (m_onNodeSelected)
            {
                if (selectedItem)
                {
                    INavigationNode *node = selectedItem->getNode();
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

        void NavigationPanelComponent::selectNode(const INavigationNode *nodeToSelect)
        {
            if (!nodeToSelect || !m_treeView.getRootItem())
            {
                spdlog::warn("selectNode called with null node or tree has no root.");
                return;
            }

            spdlog::info("selectNode: Attempting to select node '{}'.", nodeToSelect->getName());

            // 1. Get the path of nodes from the data model. This is always reliable.
            const auto path = getNodePath(nodeToSelect);

            // The first element in the path should be our root node model.
            if (path.empty() || path.front() != m_currentRootNode)
            {
                spdlog::error("selectNode: Node path is invalid or doesn't start from the current root.");
                return;
            }

            // 2. Traverse the GUI tree, building it as we go.
            juce::TreeViewItem *currentItem = m_treeView.getRootItem();

            // We start from the second item in the path, as the first is the root.
            for (size_t i = 1; i < path.size(); ++i)
            {
                auto *targetChildNode = path[i];

                // This is the key: If the current GUI item's children haven't been built, build them now.
                auto *currentNavItem = dynamic_cast<NavTreeViewItem *>(currentItem);
                if (currentNavItem && !currentNavItem->isOpen())
                {
                    // Opening the item will trigger buildSubItems(), creating the next level of the GUI.
                    currentNavItem->setOpen(true);
                }

                // Now find the specific child item we need for the next step of the path.
                juce::TreeViewItem *nextItem = nullptr;
                for (int j = 0; j < currentItem->getNumSubItems(); ++j)
                {
                    auto *subItem = dynamic_cast<NavTreeViewItem *>(currentItem->getSubItem(j));
                    // Compare by unique ID instead of pointer, since we might have different instances
                    if (subItem && subItem->getNode() && 
                        subItem->getNode()->getUniqueId() == targetChildNode->getUniqueId())
                    {
                        nextItem = subItem;
                        break;
                    }
                }

                if (nextItem == nullptr)
                {
                    spdlog::error("selectNode: Failed to find GUI item for node '{}' while traversing path.", targetChildNode->getName());
                    return; // Abort if we can't build the GUI path
                }
                currentItem = nextItem;
            }

            // 3. At the end of the loop, 'currentItem' is the TreeViewItem we want to select.
            if (currentItem)
            {
                currentItem->setSelected(true, true);
                
                // Defer scrolling to ensure the tree has finished laying out
                // This is especially important when items have just been expanded
                juce::MessageManager::callAsync([this, currentItem]()
                {
                    if (currentItem != nullptr && currentItem->isSelected())
                    {
                        m_treeView.scrollToKeepItemVisible(currentItem);
                        spdlog::info("selectNode: Scrolled to keep item visible.");
                    }
                });
                
                spdlog::info("selectNode: Successfully selected item.");
            }
        }

        NavTreeViewItem *NavigationPanelComponent::findTreeViewItemForNode(INavigationNode *targetNode)
        {
            return findTreeViewItemForNode(m_treeView.getRootItem(), targetNode);
        }

        NavTreeViewItem *NavigationPanelComponent::findTreeViewItemForNode(juce::TreeViewItem *currentItem, INavigationNode *targetNode)
        {
            if (currentItem == nullptr)
            {
                return nullptr;
            }

            auto *navItem = dynamic_cast<NavTreeViewItem *>(currentItem);
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

        void NavigationPanelComponent::refreshNode(INavigationNode *nodeToRefresh)
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

        void NavigationPanelComponent::expand(INavigationNode *nodeToExpand)
        {
            if (!m_currentRootNode || !nodeToExpand || !m_treeView.getRootItem())
            {
                return;
            }

            nodeToExpand->refreshChildren();

            // Find the TreeViewItem associated with the nodeToRemove
            const auto treeViewItemToRefresh = findTreeViewItemForNode(nodeToExpand);
            if (!treeViewItemToRefresh)
            {
                return;
            }
            // 5. Trigger the GUI update for this TreeViewItem's children
            const std::string strDisplayNode{treeViewItemToRefresh->getNode() ? treeViewItemToRefresh->getNode()->getName() : "UNKNOWN_NODE_IN_GUI_ITEM"};
            spdlog::info("NavigationPanel::refreshNode - Refreshing GUI sub-items for "
                         "TreeViewItem displaying node: {}",
                strDisplayNode);

            treeViewItemToRefresh->clearSubItems(); 

            treeViewItemToRefresh->rebuildSubItemsFromModel(); // You will create this method

            if (!treeViewItemToRefresh->isOpen())
            {
                treeViewItemToRefresh->setOpen(true); // Open it to trigger buildSubItems
            }
            m_treeView.repaint(); // Ensure the tree view repaints
        }
        
        bool NavigationPanelComponent::expandPathAndSelectTarget(const std::vector<INavigationNode*>& pathFromRoot)
        {
            if (pathFromRoot.empty() || !m_treeView.getRootItem())
            {
                return false;
            }
            
            // First we need to find the Folders node in the tree
            NavTreeViewItem* foldersTreeItem = nullptr;
            auto* rootTreeItem = m_treeView.getRootItem();
            
            // Look for the Folders node among root's children
            for (int i = 0; i < rootTreeItem->getNumSubItems(); ++i)
            {
                auto* item = dynamic_cast<NavTreeViewItem*>(rootTreeItem->getSubItem(i));
                if (item && item->getNode() && item->getNode()->getName() == "Folders")
                {
                    foldersTreeItem = item;
                    break;
                }
            }
            
            if (!foldersTreeItem)
            {
                spdlog::error("expandPathAndSelectTarget: Could not find Folders node in tree");
                return false;
            }
            
            // Make sure the Folders node is expanded
            if (!foldersTreeItem->isOpen())
            {
                foldersTreeItem->setOpen(true);
            }
            
            // Start from the Folders item
            juce::TreeViewItem* currentTreeItem = foldersTreeItem;
            
            // Navigate through the path
            for (size_t i = 0; i < pathFromRoot.size(); ++i)
            {
                auto targetNode = pathFromRoot[i];
                
                // Find the tree item for this node
                NavTreeViewItem* navItem = nullptr;
                
                // Search among the current item's children
                for (int j = 0; j < currentTreeItem->getNumSubItems(); ++j)
                {
                    auto* subItem = dynamic_cast<NavTreeViewItem*>(currentTreeItem->getSubItem(j));
                    if (subItem && subItem->getNode())
                    {
                        // For VirtualFolderNodes, match by folder ID (getUniqueId returns the folder ID)
                        // This handles the case where tree shows "D:\MP3" but path has node named "MP3"
                        if (subItem->getNode()->getUniqueId() == targetNode->getUniqueId())
                        {
                            navItem = subItem;
                            spdlog::debug("Found node by folder ID match: tree node '{}' matches path node '{}' (ID: {})", 
                                        subItem->getNode()->getName(), targetNode->getName(), targetNode->getUniqueId());
                            break;
                        }
                        
                        // Fallback: check if this is the exact same node instance
                        if (subItem->getNode() == targetNode)
                        {
                            navItem = subItem;
                            spdlog::debug("Found node by pointer match: {} (ID: {})", 
                                        subItem->getNode()->getName(), targetNode->getUniqueId());
                            break;
                        }
                    }
                }
                
                // If we didn't find it and the parent might have children, expand it
                if (!navItem && currentTreeItem->mightContainSubItems())
                {
                    // Expand the parent to create child items
                    currentTreeItem->setOpen(true);
                    
                    // Try again to find the item
                    for (int j = 0; j < currentTreeItem->getNumSubItems(); ++j)
                    {
                        auto* subItem = dynamic_cast<NavTreeViewItem*>(currentTreeItem->getSubItem(j));
                        if (subItem && subItem->getNode())
                        {
                            // Match by folder ID first
                            if (subItem->getNode()->getUniqueId() == targetNode->getUniqueId())
                            {
                                navItem = subItem;
                                spdlog::debug("Found node after expansion by folder ID: tree node '{}' matches path node '{}' (ID: {})", 
                                            subItem->getNode()->getName(), targetNode->getName(), targetNode->getUniqueId());
                                break;
                            }
                            // Fallback to pointer match
                            if (subItem->getNode() == targetNode)
                            {
                                navItem = subItem;
                                break;
                            }
                        }
                    }
                }
                
                if (!navItem)
                {
                    spdlog::error("expandPathAndSelectTarget: Failed to find or create tree item for node '{}'", targetNode->getName());
                    return false;
                }
                
                // If this is the last node in the path, select it
                if (i == pathFromRoot.size() - 1)
                {
                    navItem->setSelected(true, true);
                    
                    // Ensure the item is visible - do it both immediately and async
                    // Immediate scroll for already laid out items
                    m_treeView.scrollToKeepItemVisible(navItem);
                    
                    // Also do async scroll in case the tree needs to finish layout after expansion
                    juce::MessageManager::callAsync([this, navItem]()
                    {
                        if (navItem != nullptr && navItem->isSelected())
                        {
                            m_treeView.scrollToKeepItemVisible(navItem);
                        }
                    });
                    
                    spdlog::info("expandPathAndSelectTarget: Successfully navigated to target folder");
                    return true;
                }
                else
                {
                    // Otherwise, make sure this node is expanded and continue
                    if (!navItem->isOpen())
                    {
                        navItem->setOpen(true);
                    }
                    currentTreeItem = navItem;
                }
            }
            
            return false;
        }

        void NavTreeViewItem::rebuildSubItemsFromModel()
        {
            // This method explicitly clears existing GUI children and rebuilds
            // them from the current state of m_node->m_children. It
            // assumes m_node->refreshChildren() (the model update)
            // has already been called.

            spdlog::debug("NavTreeViewItem (Node: {}): Rebuilding sub-items from model.", m_node ? m_node->getName() : "null");

            // 1. Clear existing GUI sub-items. This is crucial.
            //    clearSubItems() destroys the child NavTreeViewItem objects,
            //    which in turn release their associated INavigationNode models.
            clearSubItems();

            // 2. Reset the flag so buildSubItems knows to run.
            //m_subItemsBuilt = false;

            // 3. Call buildSubItems to repopulate with new GUI items based on
            // the (already refreshed) model.
            //    This will call m_node->expand() which should
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

        void NavigationPanelComponent::removeNodeFromTree(INavigationNode *nodeToRemove)
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