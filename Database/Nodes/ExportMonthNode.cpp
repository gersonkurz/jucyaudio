#include <Database/Nodes/ExportMonthNode.h>
#include <Database/Nodes/MixNode.h>
#include <spdlog/spdlog.h>
#include <array>
#include <algorithm>

namespace jucyaudio
{
    namespace database
    {
        ExportMonthNode::ExportMonthNode(INavigationNode* parent, const std::string& folderName,
                                         int year, int month, const std::vector<MixInfo>& mixes)
            : BaseNode{parent, getMonthName(month), "Month", "Months"}
            , m_folderName{folderName}
            , m_year{year}
            , m_month{month}
            , m_mixes{mixes}
        {
        }

        bool ExportMonthNode::expand(std::vector<INavigationNode*>& outChildren)
        {
            if (!m_mixNodesCreated)
            {
                createMixNodes();
                m_mixNodesCreated = true;
            }

            outChildren.clear();
            for (const auto& child : m_children)
            {
                child->retain(REFCOUNT_DEBUG_ARGS);
                outChildren.push_back(child);
            }
            return true;
        }

        bool ExportMonthNode::canExpand()
        {
            return !m_mixes.empty();
        }

        void ExportMonthNode::createMixNodes()
        {
            m_children.clear();

            // Sort mixes by export date (newest first)
            auto sortedMixes = m_mixes;
            std::sort(sortedMixes.begin(), sortedMixes.end(),
                     [](const MixInfo& a, const MixInfo& b) {
                         if (a.exportedAt.has_value() && b.exportedAt.has_value())
                         {
                             return a.exportedAt.value() > b.exportedAt.value();
                         }
                         return false;
                     });

            // Create MixNode for each mix
            for (const auto& mix : sortedMixes)
            {
                m_children.emplace_back(new MixNode{this, mix});
            }

            spdlog::debug("ExportMonthNode '{}/{}' created {} mix nodes",
                         m_year, getMonthName(m_month), m_children.size());
        }

        std::string ExportMonthNode::getMonthName(int month) const
        {
            static const std::array<const char*, 12> monthNames = {
                "January", "February", "March", "April", "May", "June",
                "July", "August", "September", "October", "November", "December"
            };

            if (month >= 0 && month < 12)
            {
                return monthNames[month];
            }
            return "Unknown";
        }
    }
}