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
            using ClientCreationMethod =
                std::function<void(INavigationNode *, TrackLibrary &,
                                   std::vector<INavigationNode *> &)>;

            explicit TypedContainerNode(
                INavigationNode *root, TrackLibrary &library,
                std::string_view name,
                ClientCreationMethod clientCreationMethod)
                : BaseNode{root, name},
                  m_library{library},
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

            INavigationNode* get(int64_t uniqueId) const override
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
                spdlog::debug("Refreshing children for TypedContainerNode: {}",
                             getName());

                if (m_children.empty())
                {
                    spdlog::debug("No children to refresh, calling client "
                                 "creation method.");
                    m_clientCreationMethod(this, m_library, m_children);
                }
                else
                {
                    spdlog::debug(
                        "Children already exist, using optimized variant");


                    // we start by creating a temporary copy of the current children
                    // so that we can safely replace the m_children vector without
                    // losing the references
                    std::vector<INavigationNode *> tempNodes;
                    std::swap(m_children, tempNodes);

                    // now we create a lookup map for the existing children
                    std::unordered_map<uint64_t, INavigationNode *>
                        existingChildrenMap;
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
                    std::vector<INavigationNode *> updatedChildren;
                    m_clientCreationMethod(this, m_library, m_children);
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
                            // Child is new, but the caller has already retained it: nothing to see here, move along
                        }
                    }
                    for(const auto& [id, child] : existingChildrenMap)
                    {
                        // Child was removed, release it
                        child->release(REFCOUNT_DEBUG_ARGS);
                    }
                }
            }

        private:
            bool getChildren(
                std::vector<INavigationNode *> &outChildren) override
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

            bool hasChildren() const override
            {
                if (m_children.empty())
                {
                    auto pme = const_cast<TypedContainerNode<T> *>(this);
                    pme->refreshChildren();
                }
                return !m_children.empty();
            }

            void refreshCache(bool flushCache = false) const override
            {
                spdlog::debug("Refreshing cache for TypedContainerNode: {}",
                             getName());
                if (flushCache || m_children.empty())
                {
                    auto pme = const_cast<TypedContainerNode<T> *>(this);
                    pme->refreshChildren();
                }
            }

        protected:
            TrackLibrary &m_library;
            const ClientCreationMethod m_clientCreationMethod;
        };
    } // namespace database
} // namespace jucyaudio
