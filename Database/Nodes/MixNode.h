#pragma once

// Database/Nodes/MixNode.h
#include <Database/Nodes/LibraryNode.h>
#include <filesystem>

namespace jucyaudio
{
    namespace database
    {
        extern const DataActions MixNodeActions;
        
        class MixNode final : public LibraryNode
        {
        public:
            explicit MixNode(INavigationNode *parent, const MixInfo &mixInfo);
            ~MixNode() override = default;

            const MixInfo& getMixInfo() const
            {
                return m_mixInfo;
            }

            int64_t getUniqueId() const override
            {
                return m_mixInfo.mixId;
            }

            static void createChildren(INavigationNode *parent, std::vector<INavigationNode *> &children);

        private:
            const DataActions &getNodeActions() const override;
            const MixInfo m_mixInfo;
        };
    } // namespace database
} // namespace jucyaudio
