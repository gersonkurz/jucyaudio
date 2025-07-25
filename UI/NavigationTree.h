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

        // --- INavigationTree Interface (remains an interface) ---
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
            bool removeTracks(INavigationNode *node, const std::vector<RowIndex_t> &rows);

            // @brief Remove the specified node from the navigation tree,
            // and also the underlying data (the mix, the working-set)
            bool deleteObject(INavigationNode *node);

            // @brief Notify the navigation tree that a mix has been created.
            // This method is called when a new mix is created, allowing the tree to update its structure.
            // @param mixId The ID of the newly created mix.
            void onMixCreated(MixId mixId);
            void onWorkingSetCreated(WorkingSetId workingSetId);

        private:
            RootNode *m_root{nullptr}; // Pointer to the root node of the navigation tree
            NavigationPanelComponent &m_npc;            // Reference to the UI that will display the navigation tree
            DataViewComponent &m_dvc;                   // Reference to the DataViewComponent for data display
        };

    } // namespace ui
} // namespace jucyaudio
