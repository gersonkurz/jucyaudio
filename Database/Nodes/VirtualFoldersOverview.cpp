#include <Database/Nodes/VirtualFolderNode.h>
#include <Database/Nodes/VirtualFoldersOverview.h>
#include <Database/TrackLibrary.h>
#include <spdlog/spdlog.h>
#include <functional>

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
            spdlog::info("VirtualFoldersOverview::findFolderNode searching for folder ID {}", targetFolderId);
            
            // First check if we need to expand to get children
            if (m_children.empty())
            {
                spdlog::debug("Children empty, expanding VirtualFoldersOverview");
                std::vector<INavigationNode*> children;
                expand(children);
                for (auto child : children)
                {
                    child->release(REFCOUNT_DEBUG_ARGS);
                }
            }
            
            spdlog::debug("Searching through {} root folders", m_children.size());
            
            // Helper lambda for recursive search
            std::function<INavigationNode*(INavigationNode*)> searchFolder = 
                [&searchFolder, targetFolderId](INavigationNode* node) -> INavigationNode* 
            {
                if (auto *folderNode = dynamic_cast<VirtualFolderNode*>(node))
                {
                    const auto currentFolderId = folderNode->getFolderId();
                    spdlog::debug("Checking folder: {} (ID: {})", node->getName(), currentFolderId);
                    
                    if (currentFolderId == targetFolderId)
                    {
                        spdlog::info("Found target folder: {} (ID: {})", node->getName(), currentFolderId);
                        node->retain(REFCOUNT_DEBUG_ARGS);
                        return node;
                    }
                    
                    // Recursively search children
                    if (folderNode->canExpand())
                    {
                        std::vector<INavigationNode*> children;
                        if (folderNode->expand(children))
                        {
                            for (auto child : children)
                            {
                                if (auto found = searchFolder(child))
                                {
                                    // Release the child reference since we're returning the found node
                                    for (auto remaining : children)
                                    {
                                        if (remaining != found)
                                        {
                                            remaining->release(REFCOUNT_DEBUG_ARGS);
                                        }
                                    }
                                    return found;
                                }
                                child->release(REFCOUNT_DEBUG_ARGS);
                            }
                        }
                    }
                }
                return nullptr;
            };
            
            // Search through our children
            for (auto child : m_children)
            {
                if (auto found = searchFolder(child))
                {
                    return found;
                }
            }
            
            // Not found
            return nullptr;
        }

    } // namespace database
} // namespace jucyaudio