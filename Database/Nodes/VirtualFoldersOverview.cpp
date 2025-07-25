#include <Database/Nodes/VirtualFoldersOverview.h>
#include <Database/Nodes/VirtualFolderNode.h>
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
            : BaseNode{parent, "Folders", NodeType::VirtualFoldersRoot}
        {
            // The name "Folders" will appear in the navigation tree
        }

        bool VirtualFoldersOverview::getChildren(std::vector<INavigationNode *> &outChildren)
        {
            auto* trackDb{theTrackLibrary.getTrackDatabase()};
            if (!trackDb)
            {
                spdlog::error("VirtualFoldersOverview: Track database not available");
                return false;
            }

            // Get root folders (parent_id = -1 means root)
            auto rootFolders{trackDb->getVirtualFolderChildren(-1)};
            
            for (const auto& folderInfo : rootFolders)
            {
                auto* folderNode{new VirtualFolderNode{this, folderInfo.folderId, folderInfo.folderName}};
                outChildren.push_back(folderNode);
            }
            
            spdlog::debug("VirtualFoldersOverview loaded {} root folders", rootFolders.size());
            return true;
        }

        const DataActions &VirtualFoldersOverview::getNodeActions() const
        {
            return EmptyActions;
        }

        const DataActions &VirtualFoldersOverview::getRowActions(RowIndex_t /*rowIndex*/) const
        {
            return EmptyActions;
        }

    } // namespace database
} // namespace jucyaudio