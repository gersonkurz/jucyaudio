// Database/Nodes/WorkingSetNode.cpp
#include <Utils/AssortedUtils.h>
#include <Database/Nodes/WorkingSetNode.h>
#include <cassert>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {

        WorkingSetNode::WorkingSetNode(INavigationNode *parent, const WorkingSetInfo &workingSet)
            : LibraryNode{parent, workingSet.name} // Call base constructor
        {
            m_queryArgs.workingSetId = workingSet.id;
        }

        void WorkingSetNode::createChildren(INavigationNode *parent, std::vector<INavigationNode *> &children)
        {
            TrackQueryArgs args{};
            const auto workingSets{theTrackLibrary.getWorkingSetManager().getWorkingSets(args)};
            for (const auto &workingSet : workingSets)
            {
                children.emplace_back(new WorkingSetNode{parent, workingSet});
            }
        }

    } // namespace database
} // namespace jucyaudio
