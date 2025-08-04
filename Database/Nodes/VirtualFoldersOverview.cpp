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
            // The name "Folders" will appear in the navigation tree
        }

        bool VirtualFoldersOverview::expand(std::vector<INavigationNode *> &outChildren)
        {
            const auto &folderDb = theTrackLibrary.getFolderDatabase();

            // Get the root folders (parentId = -1) from our new, fast, cached folder database.
            const auto rootFolders = folderDb.getChildFolders(-1);

            for (const auto &folderInfo : rootFolders)
            {
                // Create the new VirtualFolderNode for each root folder.
                outChildren.push_back(new VirtualFolderNode{this, folderInfo});
            }

            spdlog::debug("VirtualFoldersOverview loaded {} root folders from the database cache.", rootFolders.size());
            return !rootFolders.empty();
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