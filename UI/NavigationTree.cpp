// Database/Nodes/MixNode.cpp
#include <UI/NavigationTree.h>
#include <UI/NavigationPanelComponent.h>
#include <UI/DataViewComponent.h>
#include <Database/Nodes/RootNode.h>

namespace jucyaudio
{
    namespace ui
    {
        NavigationTree::~NavigationTree()
        {
            if (m_root)
            {
                m_root->release(REFCOUNT_DEBUG_ARGS); // Release the root node
                m_root = nullptr;                     // Clear the pointer
            }
        }

        bool NavigationTree::initialize()
        {
            assert(m_root == nullptr);         // Ensure we only initialize once
            m_root = new RootNode{};           // Create the root node
            if (!m_root)
            {
                spdlog::error("Failed to create root node for NavigationTree.");
                return false;
            }
            if (!m_npc.setRootNode(m_root))
            {
                return false;
            }

            // get first child of the root node - or rather, all of them
            assert(m_root->canExpand()); // The root node should be expandable
            std::vector<INavigationNode *> children;
            if (m_root->expand(children))
            {
                // now we can add the children to the navigation panel
                if (const auto child{ children.front() })
                {
                    m_npc.selectNode(child);
                }
                for (const auto child : children)
                {
                    child->release(REFCOUNT_DEBUG_ARGS);
                }
            }
            return true;
        }

        bool NavigationTree::deleteObject(INavigationNode* node)
        {
            // TODO: tell parent to update the visuals
            const bool deleted = node->deleteThisObject();
            if (deleted)
            {
                const auto parent{node->getParent()};

                // tell the parent that this node has been deleted
                parent->nodeHasBeenDeleted(node);

                // in the navigation panel, we need to remove the node from the tree
                m_npc.removeNodeFromTree(node);

                // also we now need to find a differnt node to select: the easiest being the parent to that node
                m_npc.selectNode(parent);
            }
            return deleted;
        }
        
        void NavigationTree::onMixCreated(MixId mixId)
        {
            if (const auto mixesRootNode{m_root->getMixesRootNode()})
            {
                m_npc.refreshNode(mixesRootNode);
                if (const auto newMixNode{mixesRootNode->get(mixId)})
                {
                    m_npc.selectNode(newMixNode);
                    newMixNode->release(REFCOUNT_DEBUG_ARGS);
                }
            }
        }

        void NavigationTree::onWorkingSetCreated(WorkingSetId workingSetId)
        {
            if (const auto workingSetsRootNode{m_root->getWorkingSetsRootNode()})
            {
                m_npc.refreshNode(workingSetsRootNode);
                if (const auto newWorkingSetNode{workingSetsRootNode->get(workingSetId)})
                {
                    m_npc.selectNode(newWorkingSetNode);
                    newWorkingSetNode->release(REFCOUNT_DEBUG_ARGS);
                }
            }
        }

        void NavigationTree::onNodeRenamed(INavigationNode *node, std::string_view newName)
        {
            node->rename(newName);
            if (auto navTreeItem = m_npc.findTreeViewItemForNode(node))
            {
                navTreeItem->getOwnerView()->repaint();
            }
        }

    } // namespace database
} // namespace jucyaudio
