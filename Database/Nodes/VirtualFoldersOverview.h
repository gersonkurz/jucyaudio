#pragma once

#include <Database/Nodes/BaseNode.h>

namespace jucyaudio
{
    namespace database
    {
        // Shows root-level virtual folders (depth = 0)
        class VirtualFoldersOverview : public BaseNode
        {
        public:
            VirtualFoldersOverview(INavigationNode *parent);
            ~VirtualFoldersOverview() override = default;

            // Navigation
            bool canExpand() override
            {
                return true;
            }
            bool expand(std::vector<INavigationNode *> &outChildren) override;
            
            // No actions on the overview node itself
            const DataActions &getNodeActions() const override;
            const DataActions &getRowActions(RowIndex_t /*rowIndex*/) const override;
            
            // Find a specific folder node by folder ID
            // Returns a retained pointer that must be released by the caller
            INavigationNode* findFolderNode(FolderId folderId);
            
            // Data view methods to show folders as rows
            const std::vector<DataColumn> &getColumns() const override;
            bool getNumberOfRows(int64_t &outCount) const override;
            CellRenderInfo getCellRenderInfo(RowIndex_t rowIndex, ColumnIndex_t columnIndex) const override;
            RowActivationResult onRowActivated(RowIndex_t rowIndex) override;
            
        private:
            // Cache for root folders
            mutable std::vector<FolderInfo> m_rootFolders;
            mutable bool m_rootFoldersLoaded = false;
            
            void loadRootFolders() const;
        };
    } // namespace database
} // namespace jucyaudio