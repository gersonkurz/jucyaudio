#include <Database/Nodes/VirtualFolderNode.h>
#include <Database/Nodes/VirtualFoldersOverview.h>
#include <Database/TrackLibrary.h>
#include <functional>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        namespace
        {
            const DataActions EmptyActions{};

            // Simple folder columns - just show name and info
            const std::vector<DataColumn> FolderColumns = {
                DataColumn{0, "title", "Name", 300, ColumnAlignment::Left, ColumnDataTypeHint::String},
                DataColumn{1, "artist_name", "Info", 150, ColumnAlignment::Left, ColumnDataTypeHint::String},
                DataColumn{2, "album_title", "", 150, ColumnAlignment::Left, ColumnDataTypeHint::String},
            };
        } // namespace

        VirtualFoldersOverview::VirtualFoldersOverview(INavigationNode *parent)
            : BaseNode{parent, "Folders", "Folder", "Folders"}
        {
        }

        bool VirtualFoldersOverview::expand(std::vector<INavigationNode *> &outChildren)
        {
            // Ensure root folders are loaded
            loadRootFolders();

            // Create navigation nodes for each root folder
            for (const auto &folderInfo : m_rootFolders)
            {
                outChildren.push_back(new VirtualFolderNode{this, folderInfo});
            }

            spdlog::debug("VirtualFoldersOverview expanded {} root folders.", m_rootFolders.size());
            return !m_rootFolders.empty();
        }

        const DataActions &VirtualFoldersOverview::getNodeActions() const
        {
            return EmptyActions;
        }

        const DataActions &VirtualFoldersOverview::getRowActions(RowIndex_t /*rowIndex*/) const
        {
            return EmptyActions;
        }

        const std::vector<DataColumn> &VirtualFoldersOverview::getColumns() const
        {
            return FolderColumns;
        }

        void VirtualFoldersOverview::loadRootFolders() const
        {
            if (m_rootFoldersLoaded)
                return;

            m_rootFolders.clear();

            // Get the library roots and convert to FolderInfo
            auto &rootManager = theTrackLibrary.getLibraryRootManager();
            auto &folderDb = theTrackLibrary.getFolderDatabase();

            const auto libraryRoots = rootManager.getAllRoots();

            for (const auto &rootInfo : libraryRoots)
            {
                const FolderId folderId = folderDb.findOrCreateFolderByPath(rootInfo.path);
                if (folderId != -1)
                {
                    if (auto folderInfoOpt = folderDb.getFolderById(folderId))
                    {
                        m_rootFolders.push_back(*folderInfoOpt);
                    }
                }
            }

            m_rootFoldersLoaded = true;
            spdlog::debug("VirtualFoldersOverview::loadRootFolders loaded {} root folders", m_rootFolders.size());
        }

        bool VirtualFoldersOverview::getNumberOfRows(int64_t &outCount) const
        {
            loadRootFolders();
            outCount = static_cast<int64_t>(m_rootFolders.size());
            return true;
        }

        CellRenderInfo VirtualFoldersOverview::getCellRenderInfo(RowIndex_t rowIndex, ColumnIndex_t columnIndex) const
        {
            CellRenderInfo info;

            loadRootFolders();

            if (rowIndex < 0 || rowIndex >= static_cast<RowIndex_t>(m_rootFolders.size()))
                return info;

            const auto &folder = m_rootFolders[rowIndex];

            // Check column type by index (matching VirtualFolderNode's approach)
            std::string columnName;
            if (columnIndex == 0)
                columnName = "title";
            else if (columnIndex == 1)
                columnName = "artist_name";
            else if (columnIndex == 2)
                columnName = "album_title";

            if (columnName == "title")
            {
                info.text = "📁 " + folder.name;
                info.state = RenderState::Accent;
            }
            else if (columnName == "artist_name")
            {
                info.text = std::to_string(folder.trackCount) + " tracks";
                info.state = RenderState::Subdued;
            }
            // Other columns remain empty

            return info;
        }

        RowActivationResult VirtualFoldersOverview::onRowActivated(RowIndex_t rowIndex)
        {
            RowActivationResult result;

            loadRootFolders();

            if (rowIndex >= 0 && rowIndex < static_cast<RowIndex_t>(m_rootFolders.size()))
            {
                const auto &folder = m_rootFolders[rowIndex];
                result.type = RowActivationResultType::NavigateToNode;
                result.newNode = new VirtualFolderNode{this, folder};
                result.newNode->retain(); // Caller must release
                spdlog::info("VirtualFoldersOverview::onRowActivated - navigating to folder '{}' (ID: {})", folder.name, folder.folderId);
            }

            return result;
        }

        INavigationNode *VirtualFoldersOverview::findFolderNode(FolderId targetFolderId)
        {
            spdlog::info("VirtualFoldersOverview::findFolderNode searching for folder ID {}", targetFolderId);

            // First check if we need to expand to get children
            if (m_children.empty())
            {
                spdlog::debug("Children empty, expanding VirtualFoldersOverview");
                std::vector<INavigationNode *> children;
                expand(children);
                for (auto child : children)
                {
                    child->release(REFCOUNT_DEBUG_ARGS);
                }
            }

            spdlog::debug("Searching through {} root folders", m_children.size());

            // Helper lambda for recursive search
            std::function<INavigationNode *(INavigationNode *)> searchFolder = [&searchFolder, targetFolderId](INavigationNode *node) -> INavigationNode *
            {
                if (auto *folderNode = dynamic_cast<VirtualFolderNode *>(node))
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
                        std::vector<INavigationNode *> children;
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