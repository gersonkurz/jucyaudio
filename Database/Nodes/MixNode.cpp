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
        const DataActions MixNodeActions{DataAction::ShowMixEditor, DataAction::ShowTrackEditor, DataAction::Separator, DataAction::EditMixMetadata, DataAction::Delete, DataAction::ExportMix};
        MixNode::MixNode(INavigationNode *parent, const MixInfo &mixInfo)
            : LibraryNode{parent, mixInfo.name, "Mix", "Mixes"},
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

        bool MixNode::removeObjects(const std::vector<ObjectId> &objectIds) const
        {
            return theTrackLibrary.getMixManager().removeTracksFromMix(m_queryArgs.mixId, objectIds);
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
