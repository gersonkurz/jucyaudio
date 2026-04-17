// UI/NavigationTree.cpp
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
            // If already initialized, release the old root first
            if (m_root != nullptr)
            {
                m_npc.releaseRootNode();
                m_root->release(REFCOUNT_DEBUG_ARGS);
                m_root = nullptr;
            }

            m_root = new RootNode{};           // Create the root node (throws std::bad_alloc on failure)

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
                if (!children.empty())
                {
                    if (const auto child{ children.front() })
                    {
                        m_npc.selectNode(child);
                    }
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
            if (node == nullptr)
            {
                return false;
            }

            auto* parent = node->getParent();
            if (parent)
            {
                parent->retain(REFCOUNT_DEBUG_ARGS);
            }

            const bool deleted = node->deleteThisObject();
            if (deleted)
            {
                if (parent)
                {
                    // Update the model immediately, but defer the TreeView rebuild until
                    // the current UI event has unwound. Deleting/rebuilding TreeView items
                    // synchronously from within the delete action can leave JUCE painting
                    // stale GUI items.
                    parent->nodeHasBeenDeleted(node);
                    juce::MessageManager::callAsync([this, parent]()
                    {
                        m_npc.clearSelection();
                        m_npc.refreshNode(parent);
                        m_npc.expand(parent);
                        m_npc.selectNode(parent);
                        parent->release(REFCOUNT_DEBUG_ARGS);
                    });
                    parent = nullptr; // Ownership transferred to async callback.
                }
            }

            if (parent)
            {
                parent->release(REFCOUNT_DEBUG_ARGS);
            }

            return deleted;
        }

        void NavigationTree::onMixCreated(MixId mixId)
        {
            spdlog::debug("[NAV] onMixCreated: mixId={}", mixId);
            if (const auto mixesRootNode{m_root->getMixesRootNode()})
            {
                m_npc.expand(mixesRootNode);
                if (const auto newMixNode{mixesRootNode->get(mixId)})
                {
                    spdlog::debug("[NAV] onMixCreated: found new mix node '{}', selecting", newMixNode->getName());
                    m_npc.selectNode(newMixNode);
                    newMixNode->release(REFCOUNT_DEBUG_ARGS);
                }
                else
                {
                    spdlog::debug("[NAV] onMixCreated: mixId={} NOT FOUND in model children after expand!", mixId);
                }
                mixesRootNode->release(REFCOUNT_DEBUG_ARGS);
            }
            else
            {
                spdlog::debug("[NAV] onMixCreated: getMixesRootNode() returned null!");
            }
        }

        void NavigationTree::onWorkingSetCreated(WorkingSetId workingSetId)
        {
            spdlog::debug("[NAV] onWorkingSetCreated: workingSetId={}", workingSetId);
            if (const auto workingSetsRootNode{m_root->getWorkingSetsRootNode()})
            {
                m_npc.expand(workingSetsRootNode);
                if (const auto newWorkingSetNode{workingSetsRootNode->get(workingSetId)})
                {
                    spdlog::debug("[NAV] onWorkingSetCreated: found node '{}', selecting", newWorkingSetNode->getName());
                    m_npc.selectNode(newWorkingSetNode);
                    newWorkingSetNode->release(REFCOUNT_DEBUG_ARGS);
                }
                else
                {
                    spdlog::debug("[NAV] onWorkingSetCreated: id={} NOT FOUND in model children after expand!", workingSetId);
                }
                workingSetsRootNode->release(REFCOUNT_DEBUG_ARGS);
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

        void NavigationTree::onMixExportStatusChanged()
        {
            spdlog::debug("[NAV] onMixExportStatusChanged: refreshing Mixes and Exported nodes");

            // Refresh the Mixes node to show/hide mixes based on export status
            if (const auto mixesRootNode{m_root->getMixesRootNode()})
            {
                m_npc.refreshNode(mixesRootNode);
                mixesRootNode->release(REFCOUNT_DEBUG_ARGS);
            }

            // Refresh the Exported node to show newly exported/unlocked mixes
            std::vector<INavigationNode *> children;
            if (m_root->expand(children))
            {
                for (auto child : children)
                {
                    if (child->getName() == "Exported")
                    {
                        spdlog::debug("[NAV] onMixExportStatusChanged: refreshing Exported node");
                        m_npc.refreshNode(child);
                        break;
                    }
                }
                for (const auto child : children)
                {
                    child->release(REFCOUNT_DEBUG_ARGS);
                }
            }
        }

    } // namespace ui
} // namespace jucyaudio
