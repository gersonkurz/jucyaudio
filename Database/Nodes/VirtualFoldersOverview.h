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
        };
    } // namespace database
} // namespace jucyaudio