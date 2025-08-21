#pragma once

#include <Database/Includes/TrackInfo.h>
#include <Database/Nodes/LibraryNode.h>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        class VirtualFolderNode : public LibraryNode
        {
        public:
            VirtualFolderNode(INavigationNode *parent, const FolderInfo &folderInfo);

            // Override INavigationNode interface
            bool canExpand() override;
            ObjectId getUniqueId() const override;
            bool expand(std::vector<INavigationNode *> &outChildren) override;
            bool getTotalTrackCount(int64_t &outCount) const override;

            int64_t getFolderId() const
            {
                return m_folderId;
            }

            // Override to check if this folder is online
            bool isOnline() const override;

        // Override Node-Centric Command Architecture methods for folder navigation
        CellRenderInfo getCellRenderInfo(RowIndex_t rowIndex, ColumnIndex_t columnIndex) const override;
        RowActivationResult onRowActivated(RowIndex_t rowIndex) override;
        
        // Override to include folders in the row count
        bool getNumberOfRows(int64_t &outCount) const override;
        
    protected:
        // Override to handle folder rows
        const TrackInfo *getTrackInfoForRow(RowIndex_t rowIndex) const override;
        
    private:
        bool parentHasDifferentType() const;

        FolderId m_folderId;
        mutable bool m_onlineStatusCached{false};
        mutable bool m_isOnline{true};
        
        // Cached folder children for hierarchical display
        mutable std::vector<FolderInfo> m_childFolders;
        mutable bool m_childFoldersLoaded{false};
        
        // Helper to determine row type
        enum class RowType { ParentFolder, ChildFolder, Track };
        RowType getRowType(RowIndex_t rowIndex) const;
        int64_t getChildFolderCount() const;
        bool hasParent() const;
        };

    } // namespace database
} // namespace jucyaudio