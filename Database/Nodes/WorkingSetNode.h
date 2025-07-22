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
            explicit WorkingSetNode(INavigationNode *parent, const WorkingSetInfo& workingSet);
            ~WorkingSetNode() override = default;

            const auto& getWorkingSetInfo() const
            {
                return m_workingSetInfo;
            }

            int64_t getUniqueId() const override
            {
                return m_queryArgs.workingSetId;
            }

            void rename(std::string_view newName)
            {
                m_workingSetInfo.name = newName;
                BaseNode::rename(newName);
            }

            static void createChildren(INavigationNode *parent, std::vector<INavigationNode *> &children);

        private:
            WorkingSetInfo m_workingSetInfo;
        };
    } // namespace database
} // namespace jucyaudio
