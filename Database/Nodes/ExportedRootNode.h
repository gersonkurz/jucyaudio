#pragma once

#include <Database/Nodes/BaseNode.h>
#include <vector>
#include <memory>

namespace jucyaudio
{
    namespace database
    {
        class ExportedRootNode : public BaseNode
        {
        public:
            explicit ExportedRootNode(INavigationNode* parent);
            ~ExportedRootNode() override = default;

            bool expand(std::vector<INavigationNode*>& outChildren) override;
            bool canExpand() override;

        private:
            void loadExportFolders();
            bool m_foldersLoaded{false};
        };
    }
}