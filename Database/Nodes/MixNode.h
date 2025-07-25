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

            ObjectId getUniqueId() const override
            {
                return m_mixInfo.mixId;
            }

            bool removeObjects(const std::vector<ObjectId> &objectIds) const override;
            bool deleteThisObject() override;
            void rename(std::string_view newName) override;
            static void createChildren(INavigationNode *parent, std::vector<INavigationNode *> &children);

        private:
            const DataActions &getNodeActions() const override;
            MixInfo m_mixInfo;
        };
    } // namespace database
} // namespace jucyaudio
