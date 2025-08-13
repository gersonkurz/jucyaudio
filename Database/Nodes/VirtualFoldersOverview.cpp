#include <Database/Nodes/VirtualFolderNode.h>
#include <Database/Nodes/VirtualFoldersOverview.h>
#include <Database/TrackLibrary.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        namespace
        {
            const DataActions EmptyActions{};
        }

        VirtualFoldersOverview::VirtualFoldersOverview(INavigationNode *parent)
            : BaseNode{parent, "Folders", "Folder", "Folders"}
        {
        }

        bool VirtualFoldersOverview::expand(std::vector<INavigationNode *> &outChildren)
        {
            // Get the required database managers from the central library instance.
            auto &rootManager = theTrackLibrary.getLibraryRootManager();
            auto &folderDb = theTrackLibrary.getFolderDatabase();

            // 1. Get the list of user-defined root paths from our new manager.
            const auto libraryRoots = rootManager.getAllRoots();

            // 2. For each configured root path, create a display node.
            for (const auto &rootInfo : libraryRoots)
            {
                // To create a VirtualFolderNode, we need the full FolderInfo struct.
                // We can get this by looking up the folder by its path.
                // NOTE: This call is robust; if a root was added but never scanned,
                // this will ensure its entry exists in the Folders table.
                const FolderId folderId = folderDb.findOrCreateFolderByPath(rootInfo.path);

                if (folderId != -1)
                {
                    // Now that we have the ID, get the full info object.
                    // This is fast, as it should hit the folder cache.
                    if (auto folderInfoOpt = folderDb.getFolderById(folderId))
                    {
                        outChildren.push_back(new VirtualFolderNode{this, *folderInfoOpt});
                    }
                    else
                    {
                        spdlog::warn("Could not retrieve FolderInfo for a known root path '{}' with ID {}", rootInfo.path, folderId);
                    }
                }
                else
                {
                    spdlog::warn("Could not find or create a folder entry for root path '{}'", rootInfo.path);
                }
            }

            spdlog::debug("VirtualFoldersOverview loaded {} custom root folders.", libraryRoots.size());
            return !libraryRoots.empty();
        }

        const DataActions &VirtualFoldersOverview::getNodeActions() const
        {
            return EmptyActions;
        }

        const DataActions &VirtualFoldersOverview::getRowActions(RowIndex_t /*rowIndex*/) const
        {
            return EmptyActions;
        }

        INavigationNode* VirtualFoldersOverview::findFolderNode(FolderId targetFolderId)
        {
            // First check if we need to expand to get children
            if (m_children.empty())
            {
                std::vector<INavigationNode*> children;
                expand(children);
                for (auto child : children)
                {
                    child->release(REFCOUNT_DEBUG_ARGS);
                }
            }
            
            // Now search through our children for the matching folder
            for (auto child : m_children)
            {
                if (auto *folderNode = dynamic_cast<VirtualFolderNode*>(child))
                {
                    if (folderNode->getFolderId() == targetFolderId)
                    {
                        child->retain(REFCOUNT_DEBUG_ARGS);
                        return child;
                    }
                    
                    // Also check if it's a child folder
                    // We need to recursively search through subfolders
                    std::vector<INavigationNode*> subChildren;
                    if (folderNode->canExpand() && folderNode->expand(subChildren))
                    {
                        for (auto subChild : subChildren)
                        {
                            if (auto *subFolderNode = dynamic_cast<VirtualFolderNode*>(subChild))
                            {
                                if (subFolderNode->getFolderId() == targetFolderId)
                                {
                                    // Found it! Return the retained pointer
                                    return subChild;
                                }
                            }
                            subChild->release(REFCOUNT_DEBUG_ARGS);
                        }
                    }
                }
            }
            
            // Not found
            return nullptr;
        }

    } // namespace database
} // namespace jucyaudio