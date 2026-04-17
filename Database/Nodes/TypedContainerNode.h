// Database/Nodes/TypedContainerNode.h
#pragma once

#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/Nodes/BaseNode.h>
#include <Database/TrackLibrary.h>
#include <algorithm> // For std::generate_n
#include <atomic>
#include <random>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        template <typename T> class TypedContainerNode : public BaseNode
        {
        public:
            using ClientCreationMethod = std::function<void(INavigationNode *, std::vector<INavigationNode *> &)>;

            explicit TypedContainerNode(INavigationNode *root,
                std::string_view name,
                ClientCreationMethod clientCreationMethod,
                std::string_view typeNameForSingleObject,
                std::string_view typeNameForMultipleObjects)
                : BaseNode{root, name, typeNameForSingleObject, typeNameForMultipleObjects},
                  m_clientCreationMethod{clientCreationMethod}
            {
            }

            ~TypedContainerNode() override
            {
                for (const auto child : m_children)
                {
                    child->release(REFCOUNT_DEBUG_ARGS);
                }
                m_children.clear();
            }

            INavigationNode *get(int64_t uniqueId) const override
            {
                for (const auto child : m_children)
                {
                    if (child->getUniqueId() == uniqueId)
                    {
                        child->retain(REFCOUNT_DEBUG_ARGS);
                        return child;
                    }
                }
                return nullptr;
            }

            void refreshChildren() override
            {
                if (m_children.empty())
                {
                    spdlog::debug("[NAV] refreshChildren '{}': m_children empty, querying DB", getName());
                    m_clientCreationMethod(this, m_children);
                    spdlog::debug("[NAV] refreshChildren '{}': DB returned {} children", getName(), m_children.size());
                }
                else
                {
                    const auto oldCount = m_children.size();
                    spdlog::debug("[NAV] refreshChildren '{}': merging (had {} children)", getName(), oldCount);

                    // we start by creating a temporary copy of the current children
                    // so that we can safely replace the m_children vector without
                    // losing the references
                    std::vector<INavigationNode *> tempNodes;
                    std::swap(m_children, tempNodes);

                    // now we create a lookup map for the existing children
                    std::unordered_map<uint64_t, INavigationNode *> existingChildrenMap;
                    for (auto child : tempNodes)
                    {
                        existingChildrenMap[child->getUniqueId()] = child;
                    }

                    // m_children has the new list, tempNodes the old one. We assume the new list
                    // is in the correct order.

                    // if a child has been removed, it needs to be released.
                    //      - that means, it will be in existingChildrenMap because nobody removed it
                    // if a child already existed before, we can reuse it.
                    //      - that means, we remove it from existingChildrenMap
                    // if a child is new, it needs to be added.
                    //      - that means, it never was in existingChildrenMap
                    m_clientCreationMethod(this, m_children);
                    const auto maxIndex = m_children.size();
                    for (size_t index = 0; index < maxIndex; ++index)
                    {
                        const auto child = m_children[index];
                        assert(child != nullptr);
                        const auto thisId = child->getUniqueId();
                        const auto it = existingChildrenMap.find(thisId);
                        if (it != existingChildrenMap.end())
                        {
                            // Child already exists, reuse it
                            child->release(REFCOUNT_DEBUG_ARGS);
                            m_children[index] = it->second;
                            existingChildrenMap.erase(it);
                        }
                        else
                        {
                            spdlog::debug("[NAV] refreshChildren '{}': new child id={}", getName(), thisId);
                        }
                    }
                    for (const auto &[id, child] : existingChildrenMap)
                    {
                        spdlog::debug("[NAV] refreshChildren '{}': removed child id={}", getName(), id);
                        child->release(REFCOUNT_DEBUG_ARGS);
                    }
                    spdlog::debug("[NAV] refreshChildren '{}': merged {} → {} children", getName(), oldCount, m_children.size());
                }
            }

        private:
            void nodeHasBeenDeleted(INavigationNode *node) override
            {
                const auto it = std::find_if(m_children.begin(),
                    m_children.end(),
                    [node](INavigationNode *child)
                    {
                        return child->getUniqueId() == node->getUniqueId();
                    });

                if (it != m_children.end())
                {
                    spdlog::debug("Node {} has been deleted, removing from children.", node->getName());
                    (*it)->release(REFCOUNT_DEBUG_ARGS);
                    m_children.erase(it);
                }
                else
                {
                    spdlog::warn("Node {} not found in children, cannot remove.", node->getName());
                }
            }

            bool expand(std::vector<INavigationNode *> &outChildren) override
            {
                assert(outChildren.empty());
                if (!m_children.empty())
                {
                    outChildren.resize(m_children.size());
                    for (size_t i = 0; i < m_children.size(); ++i)
                    {
                        outChildren[i] = m_children[i];
                        outChildren[i]->retain(REFCOUNT_DEBUG_ARGS);
                    }
                }
                return true;
            }

            bool canExpand() override
            {
                if (m_children.empty())
                {
                    spdlog::debug("[NAV] canExpand '{}': m_children empty, triggering refreshChildren()", getName());
                    auto pme = const_cast<TypedContainerNode<T> *>(this);
                    pme->refreshChildren();
                }
                return !m_children.empty();
            }

            void refreshCache(bool flushCache = false) const override
            {
                spdlog::debug("Refreshing cache for TypedContainerNode: {}", getName());
                if (flushCache || m_children.empty())
                {
                    auto pme = const_cast<TypedContainerNode<T> *>(this);
                    pme->refreshChildren();
                }
            }

        protected:
            const ClientCreationMethod m_clientCreationMethod;
        };
    } // namespace database
} // namespace jucyaudio
