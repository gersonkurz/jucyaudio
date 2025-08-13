#include <Database/Nodes/BaseNode.h>
#include <cassert>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {
        const DataActions NoActionsPossible;
        const std::vector<DataColumn> NoColumnsPossible = {};

#ifdef USE_REFCOUNT_DEBUGGING
        std::unordered_set<BaseNode *> theBaseNodes;
        std::mutex mapMutex;

        void insert_safely(BaseNode *key)
        {
            std::lock_guard<std::mutex> lock{mapMutex};
            theBaseNodes.emplace(key);
        }

        void remove_safely(BaseNode *key)
        {
            std::lock_guard<std::mutex> lock{mapMutex};
            theBaseNodes.erase(key);
        }
#endif
        bool BaseNode::removeObjects(const std::vector<ObjectId> &objectIds) const
        {
            return false;
        }

        WorkingSetId BaseNode::getWorkingSetId() const
        {
            return 0;
        }

        void BaseNode::nodeHasBeenDeleted(INavigationNode *node)
        {
        }
        void BaseNode::rename(std::string_view newName)
        {
            m_name = newName;
        }
        BaseNode::BaseNode(INavigationNode *parent,
            std::string_view name,
            std::string_view typeNameForSingleObject,
            std::string_view typeNameForMultipleObjects)
            : INavigationNode{typeNameForSingleObject, typeNameForMultipleObjects},
              m_parent{parent},
              m_name{name},
              m_refCount{1} // Start with refcount 1
        {
#ifdef USE_REFCOUNT_DEBUGGING
            insert_safely(this);
#endif
        }

        BaseNode::~BaseNode()
        {
            assert(m_refCount.load() == 0); // Ensure no leaks
            clear();
            for (const auto child : m_children)
            {
                child->release(REFCOUNT_DEBUG_ARGS);
            }
            m_children.clear();
#ifdef USE_REFCOUNT_DEBUGGING
            remove_safely(this);
#endif
        }

        INavigationNode *BaseNode::get([[maybe_unused]] int64_t uniqueId) const
        {
            return nullptr;
        }

        INavigationNode *BaseNode::get([[maybe_unused]] const std::string &name) const
        {
            return nullptr;
        }

        const std::vector<DataColumn> &BaseNode::getColumns() const
        {
            return NoColumnsPossible;
        }

        const TrackInfo *BaseNode::getTrackInfoForRow([[maybe_unused]] RowIndex_t rowIndex) const
        {
            return nullptr;
        }

        ObjectId BaseNode::getObjectIdForRow(RowIndex_t rowIndex) const
        {
            return 0;
        }

        bool BaseNode::prepareToShowData()
        {
            return true;
        }

        void BaseNode::dataNoLongerShowing()
        {
        }

        bool BaseNode::getNumberOfRows(int64_t &outCount) const
        {
            outCount = 0; // Default implementation returns 0 rows
            return true;  // Assume success
        }

        bool BaseNode::getTotalTrackCount(int64_t &outCount) const
        {
            // Default implementation: for most nodes, total track count equals row count
            return getNumberOfRows(outCount);
        }

        std::string BaseNode::getCellText([[maybe_unused]] RowIndex_t rowIndex, [[maybe_unused]] ColumnIndex_t index) const
        {
            return {};
        }

        void BaseNode::clear()
        {
        }

        ObjectId BaseNode::getUniqueId() const
        {
            return reinterpret_cast<ObjectId>(this); // Use pointer address as unique ID
        }

        void BaseNode::refreshChildren()
        {
        }

        void BaseNode::retain(REFCOUNT_DEBUG_SPEC) const
        {
#ifdef USE_REFCOUNT_DEBUGGING
            const auto value = ++m_refCount;
            spdlog::debug("BaseNode::retain {:p} at {}[{}]: is now {}", (void *)this, file, line, value);
#else
            ++m_refCount;
#endif
        }

        void BaseNode::release(REFCOUNT_DEBUG_SPEC) const
        {
#ifdef USE_REFCOUNT_DEBUGGING
            const auto value = --m_refCount;
            if (value == 0)
            {
                spdlog::warn("BaseNode::release {:p} at {}[{}]: is now {} <- delete this", (void *)this, file, line, value);
                delete this;
            }
            else
            {
                spdlog::debug("BaseNode::release {:p} at {}[{}]: is now {}", (void *)this, file, line, value);
            }
#else
            if (--m_refCount == 0)
            {
                delete this;
            }
#endif
        }

        INavigationNode *BaseNode::getParent() const
        {
            return m_parent;
        }

        bool BaseNode::deleteThisObject()
        {
            return false;
        }

        void BaseNode::removeObjectAtRow(RowIndex_t rowIndex)
        {
            spdlog::info("Not implemented: BaseNode::removeObjectAtRow({})", rowIndex);
            // Default implementation does nothing, derived classes should override if needed
        }

        const TrackQueryArgs *BaseNode::getQueryArgs() const
        {
            return nullptr;
        }

        const std::string &BaseNode::getName() const
        {
            return m_name;
        }

        bool BaseNode::setSortOrder([[maybe_unused]] const std::vector<SortOrderInfo> &sortOrders)
        {
            return true; // Assume setting sort order is always successful
        }

        std::vector<SortOrderInfo> BaseNode::getCurrentSortOrder() const
        {
            return {};
        }

        bool BaseNode::setSearchTerms([[maybe_unused]] const std::vector<std::string> &searchTerms)
        {
            return true;
        }

        std::vector<std::string> BaseNode::getCurrentSearchTerms() const
        {
            return {};
        }

        const DataActions &BaseNode::getNodeActions() const
        {
            return NoActionsPossible;
        }

        const DataActions &BaseNode::getRowActions([[maybe_unused]] RowIndex_t rowIndex) const
        {
            return NoActionsPossible;
        }

        std::vector<TrackId> BaseNode::getAllTrackIds() const
        {
            return {}; // Default implementation returns an empty list
        }

        NodePath getNodePath(const INavigationNode *targetNode)
        {
            NodePath path;
            if (targetNode)
            {
                auto *current = targetNode;
                while (current != nullptr)
                {
                    current->retain(REFCOUNT_DEBUG_ARGS); // Retain the current node
                    path.push_back(current);
                    current = current->getParent();
                }
                std::reverse(path.begin(), path.end()); // We want the path from root to target
            }
            return path;
        }

        // Revised INavigationNode interface
        bool BaseNode::canExpand()
        {
            return false;
        }

        bool BaseNode::expand(std::vector<INavigationNode *> &children)
        {
            return false;
        }

        bool BaseNode::collapse()
        {
            return true;
        }

    } // namespace database
} // namespace jucyaudio
