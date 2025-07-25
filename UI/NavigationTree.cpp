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
            return m_npc.setRootNode(m_root);
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
        
        bool NavigationTree::removeTracks(INavigationNode *node, const std::vector<RowIndex_t> &rows)
        {
            // what to do depends entirely on the context of the operation
            bool success = false;
            const auto nrSelectedRows{rows.size()};
            std::string statusMessage;
            const auto trackIds{m_dvc.getUnderlyingObjectIds<TrackId>(rows)};
            success = node->removeTracks(trackIds);
            statusMessage = success ? "Removed tracks from mix." : "Failed to remove tracks from mix.";
            if (success)
            {
                // now we need to remove the rows from the nodes' cache
                for (const auto rowIndex : rows)
                {
                    node->removeObjectAtRow(rowIndex);
                }

                // this is not quite true; we should probably instead just remove the individual items!
                m_npc.refreshNode(node);

                // refresh objects in the node itself.
                m_dvc.refreshView(); // Refresh data view if it's the current view
            }
            return success;
        }

    } // namespace database
} // namespace jucyaudio
