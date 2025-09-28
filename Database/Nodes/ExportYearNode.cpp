#include <Database/Nodes/ExportYearNode.h>
#include <Database/Nodes/ExportMonthNode.h>
#include <chrono>
#include <map>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        ExportYearNode::ExportYearNode(INavigationNode* parent, const std::string& folderName,
                                       int year, const std::vector<MixInfo>& mixes)
            : BaseNode{parent, std::to_string(year), "Year", "Years"}
            , m_folderName{folderName}
            , m_year{year}
            , m_mixes{mixes}
        {
        }

        bool ExportYearNode::expand(std::vector<INavigationNode*>& outChildren)
        {
            if (!m_monthsCreated)
            {
                createMonthNodes();
                m_monthsCreated = true;
            }

            outChildren.clear();
            for (const auto& child : m_children)
            {
                child->retain(REFCOUNT_DEBUG_ARGS);
                outChildren.push_back(child);
            }
            return true;
        }

        bool ExportYearNode::canExpand()
        {
            return !m_mixes.empty();
        }

        void ExportYearNode::createMonthNodes()
        {
            m_children.clear();

            // Group mixes by month
            std::map<int, std::vector<MixInfo>> mixesByMonth;
            for (const auto& mix : m_mixes)
            {
                if (mix.exportedAt.has_value())
                {
                    auto time_t_val = std::chrono::system_clock::to_time_t(mix.exportedAt.value());
                    std::tm* tm = std::localtime(&time_t_val);
                    int month = tm->tm_mon; // 0-11

                    mixesByMonth[month].push_back(mix);
                }
            }

            // Create month nodes (in reverse order - newest first)
            for (auto it = mixesByMonth.rbegin(); it != mixesByMonth.rend(); ++it)
            {
                m_children.emplace_back(new ExportMonthNode{this, m_folderName, m_year, it->first, it->second});
            }

            spdlog::debug("ExportYearNode '{}' created {} month nodes", m_year, m_children.size());
        }
    }
}