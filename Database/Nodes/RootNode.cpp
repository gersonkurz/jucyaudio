#include <Database/Nodes/LibraryNode.h>
#include <Database/Nodes/LogicalFolderNode.h>
#include <Database/Nodes/MixNode.h>
#include <Database/Nodes/MixesOverview.h>
#include <Database/Nodes/RootNode.h>
#include <Database/Nodes/TypedContainerNode.h>
#include <Database/Nodes/TypedItemsOverview.h>
#include <Database/Nodes/TypedOverviewNode.h>
#include <Database/Nodes/VirtualFoldersOverview.h>
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
            const auto tokens{splitString(name, "/", true)};
            const BaseNode *pNode{this};
            for (uint32_t tokenIndex = 0; tokenIndex < tokens.size(); ++tokenIndex)
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

        RootNode::RootNode()
            : BaseNode{nullptr, "Root", NodeType::Root, "", ""}
        {
            m_children.emplace_back(new LibraryNode{this, "", NodeType::LibraryRoot, "Library", "Libraries"});
            // Use VirtualFoldersOverview instead of filesystem-based folders
            m_children.emplace_back(new VirtualFoldersOverview{this});
            m_children.emplace_back(new TypedOverviewNode<WorkingSetInfo, WorkingSetNode>{
                this, getWorkingSetsRootNodeName(), &WorkingSetNode::createChildren, NodeType::WorkingSetsRoot, "Working Set", "Working Sets"});
            m_children.emplace_back(
                new TypedOverviewNode<MixInfo, MixNode>{this, getMixesRootNodeName(), &MixNode::createChildren, NodeType::MixesRoot, "Mix", "Mixes"});
        }

        bool RootNode::expand(std::vector<INavigationNode *> &outChildren)
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

        bool RootNode::canExpand()
        {
            return true;
        }

    } // namespace database
} // namespace jucyaudio
