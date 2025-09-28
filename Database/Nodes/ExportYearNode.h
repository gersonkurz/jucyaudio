#pragma once

#include <Database/Nodes/BaseNode.h>
#include <Database/Includes/MixInfo.h>
#include <vector>
#include <string>

namespace jucyaudio
{
    namespace database
    {
        class ExportYearNode : public BaseNode
        {
        public:
            ExportYearNode(INavigationNode* parent, const std::string& folderName,
                          int year, const std::vector<MixInfo>& mixes);
            ~ExportYearNode() override = default;

            bool expand(std::vector<INavigationNode*>& outChildren) override;
            bool canExpand() override;

        private:
            void createMonthNodes();

            std::string m_folderName;
            int m_year;
            std::vector<MixInfo> m_mixes;
            bool m_monthsCreated{false};
        };
    }
}