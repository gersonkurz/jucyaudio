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
            // Set the query args to filter tracks by this folder only (not recursive).
            m_queryArgs.folderIds.push_back(m_folderId);
            m_queryArgs.recursive = false;
            spdlog::info("VirtualFolderNode created for folder '{}' (ID: {}) with recursive={}", 
                        folderInfo.name, m_folderId, m_queryArgs.recursive);
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
                outChildren.push_back(new VirtualFolderNode{this, childInfo});
            }

            return !folderChildren.empty();
        }

        bool VirtualFolderNode::isOnline() const
        {
            // Use cached value if available
            if (m_onlineStatusCached)
            {
                //spdlog::debug("VirtualFolderNode::isOnline for '{}' (ID {}): returning cached value {}", 
                 //            getName(), m_folderId, m_isOnline);
                return m_isOnline;
            }

            //spdlog::info("VirtualFolderNode::isOnline checking folder '{}' (ID {})", getName(), m_folderId);

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
            //spdlog::info("  Folder path: '{}'", folderPath);

            // Check which library root this folder belongs to
            auto &rootManager = theTrackLibrary.getLibraryRootManager();
            const auto roots = rootManager.getAllRoots();
            
            //spdlog::info("  Checking against {} library roots", roots.size());
            
            for (const auto &root : roots)
            {
                //spdlog::debug("    Comparing folder '{}' with root '{}' (online: {})", 
                //             folderPath, root.path, root.isOnline);
                
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
                    //spdlog::info("  -> Folder belongs to root '{}', isOnline = {}", 
                    //            root.path, m_isOnline);
                    return m_isOnline;
                }
            }

            // Folder doesn't belong to any root - consider it offline
            spdlog::warn("  -> Folder doesn't belong to any root, marking as OFFLINE");
            m_isOnline = false;
            m_onlineStatusCached = true;
            return false;
        }

        bool VirtualFolderNode::hasParent() const
        {
            auto &folderDb = theTrackLibrary.getFolderDatabase();
            auto folderInfo = folderDb.getFolderById(m_folderId);
            bool hasParent = folderInfo && folderInfo->parentId >= 0;
            //spdlog::debug("VirtualFolderNode::hasParent() for folder ID {} = {}", m_folderId, hasParent);
            return hasParent;
        }

        int64_t VirtualFolderNode::getChildFolderCount() const
        {
            if (!m_childFoldersLoaded)
            {
                auto &folderDb = theTrackLibrary.getFolderDatabase();
                m_childFolders = folderDb.getChildFolders(m_folderId);
                m_childFoldersLoaded = true;
            }
            return static_cast<int64_t>(m_childFolders.size());
        }

        VirtualFolderNode::RowType VirtualFolderNode::getRowType(RowIndex_t rowIndex) const
        {
            RowIndex_t currentRow = 0;
            
            // First row is parent ".." if we have a parent
            if (hasParent())
            {
                if (rowIndex == currentRow)
                {
                    //spdlog::debug("VirtualFolderNode::getRowType - row {} is ParentFolder", rowIndex);
                    return RowType::ParentFolder;
                }
                currentRow++;
            }
            
            // Next rows are child folders
            RowIndex_t childFolderCount = getChildFolderCount();
            if (childFolderCount > 0 && rowIndex < currentRow + childFolderCount)
            {
                // spdlog::debug("VirtualFolderNode::getRowType - row {} is ChildFolder", rowIndex);
                return RowType::ChildFolder;
            }
            
            // Remaining rows are tracks
            // spdlog::debug("VirtualFolderNode::getRowType - row {} is Track", rowIndex);
            return RowType::Track;
        }

        bool VirtualFolderNode::getNumberOfRows(int64_t &outCount) const
        {
            // Get track count - lazy load if needed
            if (m_cachedRowCount == -1)
            {
                m_cachedRowCount = theTrackLibrary.getTotalTrackCount(m_queryArgs);
                spdlog::info("VirtualFolderNode::getNumberOfRows - loaded track count for folder ID {}: {}", 
                            m_folderId, m_cachedRowCount);
            }
            int64_t trackCount = m_cachedRowCount;
            
            // Add folder rows
            int64_t folderRows = getChildFolderCount();
            if (hasParent())
                folderRows++; // Add 1 for parent ".."
            
            outCount = folderRows + trackCount;
            
            //spdlog::debug("VirtualFolderNode::getNumberOfRows for '{}' (ID {}): {} folders + {} tracks = {} total", 
            //             getName(), m_folderId, folderRows, trackCount, outCount);
            
            return true;
        }

        const TrackInfo *VirtualFolderNode::getTrackInfoForRow(RowIndex_t rowIndex) const
        {
            // Determine what type of row this is
            RowType rowType = getRowType(rowIndex);
            
            if (rowType != RowType::Track)
            {
                // Not a track row
                return nullptr;
            }
            
            // Calculate the adjusted track index
            int64_t trackOffset = 0;
            if (hasParent())
                trackOffset++;
            trackOffset += getChildFolderCount();
            
            // Call base implementation with adjusted index
            int64_t adjustedIndex = rowIndex - trackOffset;
            spdlog::debug("VirtualFolderNode::getTrackInfoForRow - row {} -> adjusted index {}", rowIndex, adjustedIndex);
            const TrackInfo* result = LibraryNode::getTrackInfoForRow(adjustedIndex);
            if (!result) {
                spdlog::warn("VirtualFolderNode::getTrackInfoForRow - LibraryNode returned nullptr for adjusted index {}", adjustedIndex);
            }
            return result;
        }

        CellRenderInfo VirtualFolderNode::getCellRenderInfo(RowIndex_t rowIndex, ColumnIndex_t columnIndex) const
        {
            CellRenderInfo info;
            
            // Check column type by index
            // We know the standard track columns from LibraryNode
            // Column 0 = Title, Column 1 = Artist, Column 2 = Album, etc.
            std::string columnName;
            if (columnIndex == 0) columnName = "title";
            else if (columnIndex == 1) columnName = "artist_name";
            else if (columnIndex == 2) columnName = "album_title";
            // For all other columns, we'll return empty text for folder rows
            RowType rowType = getRowType(rowIndex);
            
            if (rowType == RowType::ParentFolder)
            {
                // Parent folder navigation
                if (columnName == "title")
                {
                    info.text = "📁 ..";
                    info.state = RenderState::Accent;
                }
                else if (columnName == "artist_name")
                {
                    info.text = "(parent folder)";
                    info.state = RenderState::Subdued;
                }
                // All other columns remain empty for parent folder row
            }
            else if (rowType == RowType::ChildFolder)
            {
                // Child folder
                int64_t folderIndex = rowIndex;
                if (hasParent())
                    folderIndex--; // Adjust for parent row
                
                if (folderIndex >= 0 && folderIndex < static_cast<int64_t>(m_childFolders.size()))
                {
                    const auto &childFolder = m_childFolders[folderIndex];

                    if (columnName == "title")
                    {
                        info.text = "📁 " + childFolder.name;

                        // Check if folder should be grayed out due to filter
                        const auto currentSearchTerms = getCurrentSearchTerms();
                        if (!currentSearchTerms.empty() && m_visibleFolderIds.find(childFolder.folderId) == m_visibleFolderIds.end())
                        {
                            info.state = RenderState::Subdued;
                        }
                        else
                        {
                            info.state = RenderState::Accent;
                        }
                    }
                    else if (columnName == "artist_name")
                    {
                        info.text = std::to_string(childFolder.trackCount) + " tracks";
                        info.state = RenderState::Subdued;
                    }
                    // All other columns remain empty for child folder rows
                }
            }
            else
            {
                // Regular track - adjust index and use base implementation
                int64_t trackOffset = 0;
                if (hasParent())
                    trackOffset++;
                trackOffset += getChildFolderCount();
                int64_t adjustedIndex = rowIndex - trackOffset;
                //spdlog::debug("VirtualFolderNode::getCellRenderInfo - row {} -> adjusted index {} for column {}", 
                //             rowIndex, adjustedIndex, columnIndex);
                return LibraryNode::getCellRenderInfo(adjustedIndex, columnIndex);
            }
            
            return info;
        }
        
        bool VirtualFolderNode::parentHasDifferentType() const
        {
            return getParent()->m_refTypeNameForSingleObject != m_refTypeNameForMultipleObjects;
        }

        RowActivationResult VirtualFolderNode::onRowActivated(RowIndex_t rowIndex)
        {
            RowActivationResult result;
            RowType rowType = getRowType(rowIndex);
            
            spdlog::info("VirtualFolderNode::onRowActivated - row: {}, rowType: {}", 
                        rowIndex, 
                        rowType == RowType::ParentFolder ? "ParentFolder" : 
                        rowType == RowType::ChildFolder ? "ChildFolder" : "Track");
            
            if (rowType == RowType::ParentFolder)
            {
                if (parentHasDifferentType())
                {
                    // move to the real raw root ;)
                    // This is a root folder, navigate back to the Folders overview
                    // The parent of this node should be VirtualFoldersOverview
                    auto *parent = getParent();

                    result.type = RowActivationResultType::NavigateToNode;
                    result.newNode = parent;
                    if (result.newNode)
                    {
                        result.newNode->retain(); // Caller must release
                    }
                }
                else
                {
                    // Navigate to parent folder
                    auto &folderDb = theTrackLibrary.getFolderDatabase();
                    auto folderInfo = folderDb.getFolderById(m_folderId);
                
                    if (folderInfo)
                    {
                        if (folderInfo->parentId >= 0)
                        {
                            // Regular parent folder - create another VirtualFolderNode
                            auto parentInfo = folderDb.getFolderById(folderInfo->parentId);
                            if (parentInfo)
                            {
                                result.type = RowActivationResultType::NavigateToNode;
                                result.newNode = new VirtualFolderNode{getParent(), *parentInfo};
                                result.newNode->retain(); // Caller must release
                            }
                        }
                        else
                        {
                            // This is a root folder, navigate back to the Folders overview
                            // The parent of this node should be VirtualFoldersOverview
                            auto *parent = getParent();

                            result.type = RowActivationResultType::NavigateToNode;
                            result.newNode = parent;
                            if (result.newNode)
                            {
                                result.newNode->retain(); // Caller must release
                            }
                        }
                    }
                }
            }
            else if (rowType == RowType::ChildFolder)
            {
                // Navigate to child folder
                // Calculate which child folder this is
                int64_t folderIndex = rowIndex;
                if (hasParent())
                    folderIndex--; // Skip the parent ".." row
                
                spdlog::info("  Child folder row - rowIndex: {}, folderIndex: {}, m_childFolders.size: {}", 
                            rowIndex, folderIndex, m_childFolders.size());
                
                if (folderIndex >= 0 && folderIndex < static_cast<int64_t>(m_childFolders.size()))
                {
                    const auto &childFolder = m_childFolders[folderIndex];
                    spdlog::info("  Creating VirtualFolderNode for child folder '{}' (ID: {})", 
                                childFolder.name, childFolder.folderId);
                    result.type = RowActivationResultType::NavigateToNode;
                    // The parent of the child folder is THIS node, not our parent!
                    result.newNode = new VirtualFolderNode{this, childFolder};
                    result.newNode->retain(); // Caller must release
                }
                else
                {
                    spdlog::error("  Invalid folder index: {} (m_childFolders.size: {})", 
                                 folderIndex, m_childFolders.size());
                }
            }
            else
            {
                // Regular track - adjust index and use base implementation to play
                int64_t trackOffset = 0;
                if (hasParent())
                    trackOffset++;
                trackOffset += getChildFolderCount();
                return BaseNode::onRowActivated(rowIndex - trackOffset);
            }
            
            return result;
        }

        TrackInfosForOperationResult VirtualFolderNode::getTrackInfosForOperation(const std::vector<RowIndex_t>& selectedRows) const
        {
            TrackInfosForOperationResult result;
            auto& folderDb = theTrackLibrary.getFolderDatabase();
            
            for (const auto& rowIndex : selectedRows)
            {
                const auto rowType = getRowType(rowIndex);
                
                if (rowType == RowType::ParentFolder)
                {
                    // ".." folder - cannot be added to working set
                    result.nonApplicableCount++;
                }
                else if (rowType == RowType::ChildFolder)
                {
                    // Get the folder index
                    int64_t folderIndex = rowIndex;
                    if (hasParent())
                        folderIndex--; // Adjust for parent ".." row
                    
                    if (folderIndex >= 0 && folderIndex < static_cast<int64_t>(m_childFolders.size()))
                    {
                        const auto& childFolder = m_childFolders[folderIndex];
                        
                        // Get all tracks recursively from this folder
                        const auto allChildFolders = folderDb.getAllChildFolders({childFolder.folderId});
                        
                        // Build query to get all tracks from these folders
                        TrackQueryArgs args;
                        args.folderIds = std::vector<FolderId>{allChildFolders.begin(), allChildFolders.end()};
                        args.recursive = false; // We already have all child folders
                        args.usePaging = false;
                        
                        // Get all tracks from these folders
                        const auto tracks = theTrackLibrary.getTracks(args);
                        result.trackInfos.insert(result.trackInfos.end(), tracks.begin(), tracks.end());
                    }
                    else
                    {
                        result.nonApplicableCount++;
                    }
                }
                else if (rowType == RowType::Track)
                {
                    // Regular track - use base implementation
                    const auto* trackInfo = getTrackInfoForRow(rowIndex);
                    if (trackInfo != nullptr)
                    {
                        result.trackInfos.push_back(*trackInfo);
                    }
                    else
                    {
                        result.nonApplicableCount++;
                    }
                }
            }
            
            return result;
        }

        bool VirtualFolderNode::setSearchTerms(const std::vector<std::string> &searchTerms)
        {
            // First, call parent implementation for track filtering
            if (!LibraryNode::setSearchTerms(searchTerms))
            {
                return false;
            }

            // Update visible folder set
            m_visibleFolderIds.clear();
            if (!searchTerms.empty())
            {
                // Get visible folders from database
                const auto* trackDb = theTrackLibrary.getTrackDatabase();
                if (trackDb)
                {
                    m_visibleFolderIds = trackDb->getFoldersContainingMatchingTracks(searchTerms);
                }
            }

            return true;
        }

        std::vector<std::string> VirtualFolderNode::getCurrentSearchTerms() const
        {
            return LibraryNode::getCurrentSearchTerms();
        }

        bool VirtualFolderNode::shouldBeSubduedInNav() const
        {
            // Use the shared filter state from TrackLibrary
            if (!theTrackLibrary.hasFolderFilter())
            {
                return false;
            }

            const auto& visibleFolders = theTrackLibrary.getVisibleFolderIds();
            if (visibleFolders.empty())
            {
                return false;
            }

            // Gray out if this folder is NOT in the visible set
            return visibleFolders.find(m_folderId) == visibleFolders.end();
        }

    } // namespace database
} // namespace jucyaudio