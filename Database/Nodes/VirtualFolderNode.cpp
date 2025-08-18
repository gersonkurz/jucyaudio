#include <Database/Nodes/VirtualFolderNode.h>
#include <Database/TrackLibrary.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>

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
            // Don't allow expansion if the folder is offline
            if (!isOnline())
            {
                spdlog::debug("VirtualFolderNode::canExpand - folder '{}' is offline, preventing expansion", getName());
                return false;
            }
            
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
            
            // Don't expand if offline
            if (!isOnline())
            {
                spdlog::info("Cannot expand offline folder '{}' (ID: {})", this->getName(), m_folderId);
                return false;
            }
            
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
                spdlog::debug("VirtualFolderNode::isOnline for '{}' (ID {}): returning cached value {}", 
                             getName(), m_folderId, m_isOnline);
                return m_isOnline;
            }

            spdlog::info("VirtualFolderNode::isOnline checking folder '{}' (ID {})", getName(), m_folderId);

            // Get the folder's full path
            auto &folderDb = theTrackLibrary.getFolderDatabase();
            auto folderInfo = folderDb.getFolderById(m_folderId);
            if (!folderInfo)
            {
                spdlog::warn("  Folder ID {} not found in database", m_folderId);
                m_isOnline = false;
                m_onlineStatusCached = true;
                return false;
            }

            const std::string folderPath = folderInfo->path;
            spdlog::info("  Folder path: '{}'", folderPath);

            // Check which library root this folder belongs to
            auto &rootManager = theTrackLibrary.getLibraryRootManager();
            const auto roots = rootManager.getAllRoots();
            
            spdlog::info("  Checking against {} library roots", roots.size());
            
            for (const auto &root : roots)
            {
                spdlog::debug("    Comparing folder '{}' with root '{}' (online: {})", 
                             folderPath, root.path, root.isOnline);
                
                // Case-insensitive comparison for macOS/Windows
                // Convert both to lowercase for comparison
                std::string folderLower = folderPath;
                std::string rootLower = root.path;
                std::transform(folderLower.begin(), folderLower.end(), folderLower.begin(), ::tolower);
                std::transform(rootLower.begin(), rootLower.end(), rootLower.begin(), ::tolower);
                
                // Check if the folder path starts with this root path
                if (folderLower.find(rootLower) == 0)
                {
                    // This folder belongs to this root - check if root is online
                    m_isOnline = root.isOnline;
                    m_onlineStatusCached = true;
                    spdlog::info("  -> Folder belongs to root '{}', isOnline = {}", 
                                root.path, m_isOnline);
                    return m_isOnline;
                }
            }

            // Folder doesn't belong to any root - consider it offline
            spdlog::warn("  -> Folder doesn't belong to any root, marking as OFFLINE");
            m_isOnline = false;
            m_onlineStatusCached = true;
            return false;
        }

    } // namespace database
} // namespace jucyaudio