// Database/Nodes/WorkingSetNode.cpp
#include <Utils/AssortedUtils.h>
#include <Database/Nodes/WorkingSetNode.h>
#include <cassert>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {

        WorkingSetNode::WorkingSetNode(INavigationNode *parent, TrackLibrary &library, const WorkingSetInfo &workingSet)
            : LibraryNode{parent, library, workingSet.name} // Call base constructor
        {
            m_queryArgs.workingSetId = workingSet.id;
        }

        void WorkingSetNode::createChildren(INavigationNode *parent, TrackLibrary &library, std::vector<INavigationNode *> &children)
        {
            TrackQueryArgs args{};
            const auto workingSets{library.getWorkingSetManager().getWorkingSets(args)};
            for (const auto &workingSet : workingSets)
            {
                children.emplace_back(new WorkingSetNode{parent, library, workingSet});
            }
        }

    } // namespace database
} // namespace jucyaudio
