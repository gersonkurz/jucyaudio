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
            bool hasChildren() const override { return true; }
            bool getChildren(std::vector<INavigationNode *> &outChildren) override;
            
            // No actions on the overview node itself
            const DataActions &getNodeActions() const override;
            const DataActions &getRowActions(RowIndex_t /*rowIndex*/) const override;
        };
    } // namespace database
} // namespace jucyaudio