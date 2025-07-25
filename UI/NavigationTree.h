#pragma once

#include <Database/Includes/INavigationNode.h>

namespace jucyaudio
{
    namespace ui
    {
        class NavigationPanelComponent;
        
        // --- INavigationTree Interface (remains an interface) ---
        class NavigationTree final
        {
        public:
            NavigationTree(NavigationPanelComponent &ui)
                : m_root{nullptr},
                  m_ui{ui} // Initialize with a reference to the UI
            {
            }
            ~NavigationTree();

            // @brief Create the root node and all its dependencies, and visualize them in the UI
            bool initialize();

        private:
            database::INavigationNode *m_root{nullptr}; // Pointer to the root node of the navigation tree
            NavigationPanelComponent &m_ui;   // Pointer to the UI that will display the navigation tree
        };

    } // namespace database
} // namespace jucyaudio
