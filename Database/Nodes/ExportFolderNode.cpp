#include <Database/Nodes/ExportFolderNode.h>
#include <Database/Nodes/ExportYearNode.h>
#include <Database/Includes/IMixManager.h>
#include <Database/TrackLibrary.h>
#include <chrono>
#include <map>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        ExportFolderNode::ExportFolderNode(INavigationNode* parent, const ExportFolderInfo& folderInfo)
            : BaseNode{parent, folderInfo.name, "Export Folder", "Export Folders"}
            , m_folderInfo{folderInfo}
        {
        }

        bool ExportFolderNode::expand(std::vector<INavigationNode*>& outChildren)
        {
            if (!m_yearsLoaded)
            {
                loadYearNodes();
                m_yearsLoaded = true;
            }

            outChildren.clear();
            for (const auto& child : m_children)
            {
                child->retain(REFCOUNT_DEBUG_ARGS);
                outChildren.push_back(child);
            }
            return true;
        }

        bool ExportFolderNode::canExpand()
        {
            return true;
        }

        void ExportFolderNode::loadYearNodes()
        {
            m_children.clear();

            auto &mixManager{database::theTrackLibrary.getMixManager()};
            // Get all mixes in this export folder
            auto mixes = mixManager.getMixesByLocation(m_folderInfo.name);

            // Group mixes by year
            std::map<int, std::vector<MixInfo>> mixesByYear;
            for (const auto& mix : mixes)
            {
                if (mix.exportedAt.has_value())
                {
                    auto time_t_val = std::chrono::system_clock::to_time_t(mix.exportedAt.value());
                    std::tm* tm = std::localtime(&time_t_val);
                    int year = tm->tm_year + 1900;

                    mixesByYear[year].push_back(mix);
                }
            }

            // Create year nodes (in reverse order - newest first)
            for (auto it = mixesByYear.rbegin(); it != mixesByYear.rend(); ++it)
            {
                m_children.emplace_back(new ExportYearNode{this, m_folderInfo.name, it->first, it->second});
            }

            spdlog::debug("ExportFolderNode '{}' loaded {} years", m_folderInfo.name, m_children.size());
        }
    }
}