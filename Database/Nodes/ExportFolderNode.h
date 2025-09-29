#pragma once

#include <Database/Nodes/BaseNode.h>
#include <Database/Includes/ExportFolderInfo.h>
#include <map>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        class ExportFolderNode : public BaseNode
        {
        public:
            ExportFolderNode(INavigationNode* parent, const ExportFolderInfo& folderInfo);
            ~ExportFolderNode() override = default;

            bool expand(std::vector<INavigationNode*>& outChildren) override;
            bool canExpand() override;
            void refreshChildren() override;

        private:
            void loadYearNodes();

            ExportFolderInfo m_folderInfo;
            bool m_yearsLoaded{false};
        };
    }
}