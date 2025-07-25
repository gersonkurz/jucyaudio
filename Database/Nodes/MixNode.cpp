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
        const DataActions MixNodeActions{DataAction::Delete, DataAction::ExportMix};

        MixNode::MixNode(INavigationNode *parent, const MixInfo &mixInfo)
            : LibraryNode{parent, mixInfo.name, NodeType::Mix, "Mix", "Mixes"},
              m_mixInfo{mixInfo}
        {
            m_queryArgs.mixId = mixInfo.mixId;
        }
        void MixNode::rename(std::string_view newName)
        {
            m_mixInfo.name = newName;
            BaseNode::rename(newName);
        }
        const DataActions &MixNode::getNodeActions() const
        {
            return MixNodeActions;
        }

        bool MixNode::deleteThisObject()
        {
            return theTrackLibrary.getMixManager().removeMix(m_queryArgs.mixId);
        }

        bool MixNode::removeTracks(const std::vector<TrackId> &trackIds) const
        {
            return theTrackLibrary.getMixManager().removeTracksFromMix(m_queryArgs.mixId, trackIds);
        }

        void MixNode::createChildren(INavigationNode *parent, std::vector<INavigationNode *> &children)
        {
            TrackQueryArgs args;
            const auto mixes{theTrackLibrary.getMixManager().getMixes(args)};
            for (const auto &mix : mixes)
            {
                children.emplace_back(new MixNode{parent, mix});
            }
        }

    } // namespace database
} // namespace jucyaudio
