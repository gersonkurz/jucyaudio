#include <Database/Nodes/LogicalFolderNode.h>
#include <Database/TrackLibrary.h> // For theTrackLibrary
#include <Utils/AssortedUtils.h>

namespace jucyaudio
{
    namespace database
    {
        LogicalFolderNode::LogicalFolderNode(INavigationNode *parent, const FolderInfo &folderInfo)
            : LibraryNode{parent, folderInfo.name, "Folder", "Folders"},
              m_folderId{folderInfo.folderId}
        {
            // Set the query args for this node to filter tracks by its folder ID.
            // This is the critical link between the navigation node and the data it displays.
            m_queryArgs.folderIds.push_back(m_folderId);
            m_queryArgs.recursive = false; // Only show tracks directly in this folder
        }

        bool LogicalFolderNode::canExpand()
        {
            // The folder can expand if it has any child folders in the database.
            // We query the IFolderDatabase instead of the filesystem.
            return !theTrackLibrary.getFolderDatabase().getChildFolders(m_folderId).empty();
        }

        bool LogicalFolderNode::expand(std::vector<INavigationNode *> &outChildren)
        {
            assert(outChildren.empty());

            // Get all child folders from the database. This is fast due to the cache.
            auto childFolderData = theTrackLibrary.getFolderDatabase().getChildFolders(m_folderId);

            for (const auto &folderInfo : childFolderData)
            {
                // For each child folder record, create a new node.
                outChildren.push_back(new LogicalFolderNode(this, folderInfo));
            }

            // The sorting is now handled by the IFolderDatabase::getChildFolders method,
            // so we don't need to sort here anymore.

            return !outChildren.empty();
        }

        void LogicalFolderNode::createRootFolderNodes(INavigationNode *parent, std::vector<INavigationNode *> &children)
        {
            // This static method is the entry point. It gets the top-level folders (parentId = -1).
            auto rootFolders = theTrackLibrary.getFolderDatabase().getChildFolders(-1);
            for (const auto &folderInfo : rootFolders)
            {
                children.emplace_back(new LogicalFolderNode(parent, folderInfo));
            }
        }

    } // namespace database
} // namespace jucyaudio