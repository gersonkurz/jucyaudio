#pragma once

#include <Database/Nodes/BaseNode.h>
#include <Database/Includes/MixInfo.h>
#include <vector>
#include <string>

namespace jucyaudio
{
    namespace database
    {
        class ExportMonthNode : public BaseNode
        {
        public:
            ExportMonthNode(INavigationNode* parent, const std::string& folderName,
                           int year, int month, const std::vector<MixInfo>& mixes);
            ~ExportMonthNode() override = default;

            bool expand(std::vector<INavigationNode*>& outChildren) override;
            bool canExpand() override;

        private:
            void createMixNodes();
            std::string getMonthName(int month) const;

            std::string m_folderName;
            int m_year;
            int m_month;
            std::vector<MixInfo> m_mixes;
            bool m_mixNodesCreated{false};
        };
    }
}