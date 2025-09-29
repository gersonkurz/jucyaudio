#include <Database/Nodes/ExportedRootNode.h>
#include <Database/Nodes/ExportFolderNode.h>
#include <Database/Includes/IMixManager.h>
#include <Database/TrackLibrary.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        ExportedRootNode::ExportedRootNode(INavigationNode* parent)
            : BaseNode{parent, "Exported", "Exported Mix", "Exported Mixes"}
        {
        }

        bool ExportedRootNode::expand(std::vector<INavigationNode*>& outChildren)
        {
            if (!m_foldersLoaded)
            {
                loadExportFolders();
                m_foldersLoaded = true;
            }

            outChildren.clear();
            for (const auto& child : m_children)
            {
                child->retain(REFCOUNT_DEBUG_ARGS);
                outChildren.push_back(child);
            }
            return true;
        }

        bool ExportedRootNode::canExpand()
        {
            return true;
        }

        void ExportedRootNode::refreshChildren()
        {
            spdlog::debug("ExportedRootNode::refreshChildren - forcing reload");

            // Store the old children temporarily
            auto oldChildren = std::move(m_children);
            m_children.clear();

            // Force reload
            m_foldersLoaded = false;
            loadExportFolders();
            m_foldersLoaded = true;

            // Now release the old children after we've created the new ones
            // This ensures any UI references remain valid during the transition
            for (const auto& child : oldChildren)
            {
                child->release(REFCOUNT_DEBUG_ARGS);
            }
        }

        void ExportedRootNode::loadExportFolders()
        {
            // Don't clear children here - let refreshChildren handle that

            auto& mixManager{ database::theTrackLibrary.getMixManager() };
            auto folders = mixManager.getExportFolders();
            for (const auto& folder : folders)
            {
                m_children.emplace_back(new ExportFolderNode{this, folder});
            }

            spdlog::debug("ExportedRootNode loaded {} export folders", m_children.size());
        }
    }
}