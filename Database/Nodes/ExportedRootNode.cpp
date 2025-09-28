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

        void ExportedRootNode::loadExportFolders()
        {
            m_children.clear();

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