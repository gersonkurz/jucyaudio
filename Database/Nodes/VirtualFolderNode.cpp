#include <Database/Nodes/VirtualFolderNode.h>
#include <Database/TrackLibrary.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        VirtualFolderNode::VirtualFolderNode(INavigationNode *parent, int64_t folderId, const std::string &folderName)
            : LibraryNode{parent, folderName},
              m_folderId{folderId}
        {
        }

        bool VirtualFolderNode::hasChildren() const
        {
            // Check if this folder has any subfolders
            auto* trackDb{theTrackLibrary.getTrackDatabase()};
            if (!trackDb)
                return false;

            return trackDb->virtualFolderHasChildren(m_folderId);
        }

        bool VirtualFolderNode::getChildren(std::vector<INavigationNode *> &outChildren)
        {
            auto* trackDb{theTrackLibrary.getTrackDatabase()};
            if (!trackDb)
                return false;

            auto folderChildren{trackDb->getVirtualFolderChildren(m_folderId)};
            
            for (const auto& childInfo : folderChildren)
            {
                auto* childNode{new VirtualFolderNode{this, childInfo.folderId, childInfo.folderName}};
                outChildren.push_back(childNode);
            }
            
            return true;
        }

        bool VirtualFolderNode::prepareToShowData()
        {
            // Set the virtual folder filter in our query args
            m_queryArgs.virtualFolderId = m_folderId;
            
            // Call parent implementation
            return LibraryNode::prepareToShowData();
        }

    } // namespace database
} // namespace jucyaudio