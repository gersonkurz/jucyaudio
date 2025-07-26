#pragma once

// Database/Nodes/MixNode.h
#include <Audio/MixProjectLoader.h>
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

            const MixInfo &getMixInfo() const
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
            const TrackInfo *getTrackInfoForRow(RowIndex_t rowIndex) const override;
            void refreshCache(bool flushCache) const override;
            static void createChildren(INavigationNode *parent, std::vector<INavigationNode *> &children);

            audio::MixProjectLoader &getMixProjectLoader() const
            {
                return m_mixProjectLoader;
            }

        private:
            const DataActions &getNodeActions() const override;
            MixInfo m_mixInfo;
            mutable audio::MixProjectLoader m_mixProjectLoader;
        };
    } // namespace database
} // namespace jucyaudio
