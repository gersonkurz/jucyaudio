#include <Database/Nodes/LibraryNode.h>
#include <Database/Nodes/LogicalFolderNode.h>
#include <Database/Nodes/MixNode.h>
#include <Database/Nodes/MixesOverview.h>
#include <Database/Nodes/RootNode.h>
#include <Database/Nodes/TypedContainerNode.h>
#include <Database/Nodes/TypedItemsOverview.h>
#include <Database/Nodes/TypedOverviewNode.h>
#include <Database/Nodes/WorkingSetNode.h>
#include <Database/Nodes/WorkingSetsOverview.h>
#include <Utils/AssortedUtils.h>
#include <cassert>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        INavigationNode *RootNode::get(const std::string &name) const
        {
            const auto tokens{split_string(name, "/", true)};
            const BaseNode *pNode{this};
            for (uint32_t tokenIndex = 0; tokenIndex < tokens.size();
                 ++tokenIndex)
            {
                const auto &token{tokens[tokenIndex]};
                if (token.empty())
                {
                    continue; // Skip empty tokens
                }
                for (const auto &child : pNode->m_children)
                {
                    if (child->getName() == token)
                    {
                        if (tokenIndex == tokens.size() - 1)
                        {
                            child->retain(REFCOUNT_DEBUG_ARGS);
                            // If this is the last token, return the node
                            return child;
                        }
                        else
                        {
                            // Move to the next level in the hierarchy
                            pNode = static_cast<const BaseNode *>(child);
                            break; // Break to continue with the next token
                        }
                    }
                }
            }
            return nullptr;
        }

        RootNode::RootNode(TrackLibrary &library)
            : BaseNode{nullptr, "Root"}
        {
            m_children.emplace_back(new LibraryNode{this, library});
            m_children.emplace_back(new TypedContainerNode<LogicalFolderNode>{
                this, library, getFoldersRootNodeName(),
                &LogicalFolderNode::createChildren});
            m_children.emplace_back(
                new TypedOverviewNode<WorkingSetInfo, WorkingSetNode>{
                    this, library, getWorkingSetsRootNodeName(),
                    &WorkingSetNode::createChildren});
            m_children.emplace_back(new TypedOverviewNode<MixInfo, MixNode>{
                this, library, getMixesRootNodeName(), &MixNode::createChildren});
        }

        bool RootNode::getChildren(std::vector<INavigationNode *> &outChildren)
        {
            assert(outChildren.empty());
            outChildren.resize(m_children.size());
            for (size_t i = 0; i < m_children.size(); ++i)
            {
                outChildren[i] = m_children[i];
                outChildren[i]->retain(REFCOUNT_DEBUG_ARGS);
            }
            return true;
        }

        bool RootNode::hasChildren() const
        {
            return true;
        }

    } // namespace database
} // namespace jucyaudio
