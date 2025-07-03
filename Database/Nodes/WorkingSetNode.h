#pragma once

// Database/Nodes/WorkingSetNode.h
#include <Database/Nodes/LibraryNode.h>
#include <filesystem>

namespace jucyaudio
{
    namespace database
    {
        class WorkingSetNode final : public LibraryNode
        {
        public:
            explicit WorkingSetNode(INavigationNode *parent, TrackLibrary &library, const WorkingSetInfo& workingSet);
            ~WorkingSetNode() override = default;

            int64_t getUniqueId() const override
            {
                return m_queryArgs.workingSetId;
            }

            static void createChildren(INavigationNode *parent, TrackLibrary &library, std::vector<INavigationNode *> &children);
        };
    } // namespace database
} // namespace jucyaudio
