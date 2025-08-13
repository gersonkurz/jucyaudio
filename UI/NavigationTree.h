#pragma once

#include <Database/Includes/INavigationNode.h>
#include <Database/Nodes/RootNode.h>

namespace jucyaudio
{
    namespace ui
    {
        using namespace database;
        class NavigationPanelComponent;
        class DataViewComponent;

        // --- INavigationTree Interface  ---
        class NavigationTree final
        {
        public:
            NavigationTree(NavigationPanelComponent &npc, DataViewComponent &dvc)
                : m_root{nullptr},
                  m_npc{npc},
                  m_dvc{dvc}
            {
            }
            ~NavigationTree();

            // @brief Create the root node and all its dependencies, and visualize them in the UI
            // @return True if the initialization was successful, false otherwise.
            bool initialize();

            // @brief Remove the tracks at the specified row indices from the current root node.
            // @param node The current root node from which to remove tracks.
            // @param rows The row indices of the tracks to remove.
            bool removeObjectsForRows(INavigationNode *node, const std::vector<RowIndex_t> &rows);

            // @brief Remove the specified node from the navigation tree,
            // and also the underlying data (the mix, the working-set)
            bool deleteObject(INavigationNode *node);

            // @brief Notify the navigation tree that a mix has been created.
            // This method is called when a new mix is created, allowing the tree to update its structure.
            // @param mixId The ID of the newly created mix.
            void onMixCreated(MixId mixId);

            // @brief Notify the navigation tree that a working set has been created.
            // This method is called when a new working set is created, allowing the tree to update its structure.
            // @param workingSetId The ID of the newly created working set.
            void onWorkingSetCreated(WorkingSetId workingSetId);

            void onNodeRenamed(INavigationNode *node, std::string_view newName);

            void releaseRootNode()
            {
                if (m_root)
                {
                    m_root->release(REFCOUNT_DEBUG_ARGS); // Release the root node to allow it to be deleted
                    m_root = nullptr;
                }
            }

            auto getRootNode() const
            {
                if (m_root)
                {
                    m_root->retain(REFCOUNT_DEBUG_ARGS); // Retain the root node to ensure it stays valid
                }
                return m_root;
            }

        private:
            RootNode *m_root{nullptr}; // Pointer to the root node of the navigation tree
            NavigationPanelComponent &m_npc;            // Reference to the UI that will display the navigation tree
            DataViewComponent &m_dvc;                   // Reference to the DataViewComponent for data display
        };

    } // namespace ui
} // namespace jucyaudio
