#include <Database/Nodes/VirtualFolderNode.h>
#include <Database/TrackLibrary.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        VirtualFolderNode::VirtualFolderNode(INavigationNode *parent, const FolderInfo &folderInfo)
            : LibraryNode{parent, folderInfo.name, "Folder", "Folders"},
              m_folderId{folderInfo.folderId}
        {
            // Set the query args to filter tracks by this folder AND ALL ITS CHILDREN.
            m_queryArgs.folderIds.push_back(m_folderId);
            m_queryArgs.recursive = true;
        }

        bool VirtualFolderNode::canExpand()
        {
            return theTrackLibrary.getFolderDatabase().hasChildren(m_folderId);
        }

        ObjectId VirtualFolderNode::getUniqueId() const
        {
            return m_folderId;
        }

        bool VirtualFolderNode::getTotalTrackCount(int64_t &outCount) const
        {
            const auto &folderDb = theTrackLibrary.getFolderDatabase();
            const auto folderInfo = folderDb.getFolderById(m_folderId);
            if (folderInfo)
            {
                outCount = folderInfo->trackCount;
                spdlog::debug("Total track count for folder ID {}: {}", m_folderId, outCount);
                return true;
            }
            else
            {
                spdlog::error("Failed to get total track count for folder ID {}", m_folderId);
                outCount = 0;
                return false;
            }
        }

        bool VirtualFolderNode::expand(std::vector<INavigationNode *> &outChildren)
        {
            spdlog::debug("Expanding VirtualFolderNode '{}' (ID: {})", this->getName(), m_folderId);
            auto &folderDb = theTrackLibrary.getFolderDatabase();
            auto folderChildren = folderDb.getChildFolders(m_folderId);

            spdlog::debug("Found {} children for folder ID {}", folderChildren.size(), m_folderId);

            for (const auto &childInfo : folderChildren)
            {
                spdlog::debug("  -> Creating child node for '{}' (ID: {})", childInfo.name, childInfo.folderId);
                outChildren.push_back(new VirtualFolderNode(this, childInfo));
            }

            return !folderChildren.empty();
        }

        bool VirtualFolderNode::isOnline() const
        {
            // Use cached value if available
            if (m_onlineStatusCached)
            {
                return m_isOnline;
            }

            // Get the folder's full path
            auto &folderDb = theTrackLibrary.getFolderDatabase();
            auto folderInfo = folderDb.getFolderById(m_folderId);
            if (!folderInfo)
            {
                m_isOnline = false;
                m_onlineStatusCached = true;
                return false;
            }

            const std::string folderPath = folderInfo->path;

            // Check which library root this folder belongs to
            auto &rootManager = theTrackLibrary.getLibraryRootManager();
            const auto roots = rootManager.getAllRoots();
            
            for (const auto &root : roots)
            {
                // Check if the folder path starts with this root path
                if (folderPath.find(root.path) == 0)
                {
                    // This folder belongs to this root - check if root is online
                    m_isOnline = root.isOnline;
                    m_onlineStatusCached = true;
                    return m_isOnline;
                }
            }

            // Folder doesn't belong to any root - consider it offline
            m_isOnline = false;
            m_onlineStatusCached = true;
            return false;
        }

    } // namespace database
} // namespace jucyaudio