// Database/Nodes/MixNode.cpp
#include <UI/NavigationTree.h>
#include <UI/NavigationPanelComponent.h>
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
            return m_ui.setRootNode(m_root);
        }
    } // namespace database
} // namespace jucyaudio
