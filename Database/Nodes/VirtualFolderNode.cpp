#include <Database/Nodes/VirtualFolderNode.h>
#include <Database/TrackLibrary.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        VirtualFolderNode::VirtualFolderNode(INavigationNode *parent, int64_t folderId, const std::string &folderName)
            : LibraryNode{parent, folderName, "Folder", "Folders"},
              m_folderId{folderId}
        {
        }

        bool VirtualFolderNode::canExpand()
        {
            // Check if this folder has any subfolders
            auto *trackDb{theTrackLibrary.getTrackDatabase()};
            if (!trackDb)
                return false;

            return trackDb->virtualFolderHasChildren(m_folderId);
        }

        bool VirtualFolderNode::expand(std::vector<INavigationNode *> &outChildren)
        {
            auto *trackDb{theTrackLibrary.getTrackDatabase()};
            if (!trackDb)
                return false;

            auto folderChildren{trackDb->getVirtualFolderChildren(m_folderId)};

            for (const auto &childInfo : folderChildren)
            {
                auto *childNode{new VirtualFolderNode{this, childInfo.folderId, childInfo.folderName}};
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

        bool VirtualFolderNode::getTotalTrackCount(int64_t &outCount) const
        {
            auto *trackDb{theTrackLibrary.getTrackDatabase()};
            if (!trackDb)
            {
                outCount = 0;
                return false;
            }

            auto totalCount{trackDb->getVirtualFolderTotalTrackCount(m_folderId)};
            if (totalCount.has_value())
            {
                outCount = totalCount.value();
                return true;
            }

            outCount = 0;
            return false;
        }

    } // namespace database
} // namespace jucyaudio