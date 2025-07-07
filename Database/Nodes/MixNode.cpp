// Database/Nodes/MixNode.cpp
#include <Database/Nodes/MixNode.h>
#include <Utils/AssortedUtils.h>
#include <cassert>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        // Careful: these are actions for the *MixNode*, not the tracks shown in the mix.
        const DataActions MixNodeActions{DataAction::RemoveMix, DataAction::ExportMix};

        MixNode::MixNode(INavigationNode *parent, TrackLibrary &library, const MixInfo &mixInfo)
            : LibraryNode{parent, library, mixInfo.name},
              m_mixInfo{mixInfo}
        {
            m_queryArgs.mixId = mixInfo.mixId;
        }

        const DataActions &MixNode::getNodeActions() const
        {
            return MixNodeActions;
        }

        void MixNode::createChildren(INavigationNode *parent, TrackLibrary &library, std::vector<INavigationNode *> &children)
        {
            TrackQueryArgs args;
            const auto mixes{library.getMixManager().getMixes(args)};
            for (const auto &mix : mixes)
            {
                children.emplace_back(new MixNode{parent, library, mix});
            }
        }

    } // namespace database
} // namespace jucyaudio
