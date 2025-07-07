#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#undef USE_REFCOUNT_DEBUGGING
#ifdef USE_REFCOUNT_DEBUGGING
#define REFCOUNT_DEBUG_ARGS __FILE__, __LINE__
#define REFCOUNT_DEBUG_SPEC const char* file, int line
#else
#define REFCOUNT_DEBUG_ARGS 
#define REFCOUNT_DEBUG_SPEC 
#endif

#include <Database/Includes/Constants.h>
#include <Database/Includes/TrackQueryArgs.h>
#include <Database/Includes/DataColumn.h>
#include <Database/Includes/IRefCounted.h>

namespace jucyaudio
{
    namespace database
    {
        // --- INavigationNode Interface (remains an interface) ---
        struct INavigationNode : public IRefCounted
        {
            virtual ~INavigationNode() = default;

            /// @brief Get the children of this navigation node.
            /// This method retrieves the children of the node and populates the provided vector.
            /// The caller is responsible for releasing the nodes when done.
            /// @param outChildren Reference to a vector that will be populated with child nodes.
            /// @return True if children were successfully retrieved, false if there are no children or an error occurred.
            /// @note The children nodes are retained by this method, so the caller must release them when done.
            virtual bool getChildren(std::vector<INavigationNode *> &outChildren) = 0;

            /// @brief Check if this navigation node has children.
            /// @return True if the node has children, false otherwise.
            virtual bool hasChildren() const = 0;

            /// @brief Refresh the children of this navigation node.
            /// This method should be called to refresh the list of children, e.g., after a change in the underlying data.
            /// It may or may not be implemented by all nodes, depending on their nature.
            /// If not implemented, it can be a no-op.
            virtual void refreshChildren() = 0;

            /// @brief Refresh the cache of this navigation node.
            /// This method is called to refresh the cached data of the node.
            /// If `flushCache` is true, it should clear the existing cache and re-fetch the data.
            /// If `flushCache` is false, it should only update the cache if it is invalid or outdated.
            /// @param flushCache If true, the cache should be cleared and re-fetched; if false, only update if necessary.
            /// @note This method is declared const because conceptually it does not modify the state of the node,
            /// but it may update internal caches or data structures.
            virtual void refreshCache(bool flushCache = false) const = 0;

            virtual INavigationNode *get(const std::string &name) const = 0;

            /// @brief Get a navigation node by its unique ID.
            /// This is relative to the current node, so it should return a
            /// child node if it exists.
            /// @param uniqueId Unique identifier of the node to retrieve.
            /// @return Pointer to the navigation node with the specified unique
            /// ID, or nullptr if not found.
            virtual INavigationNode *get(int64_t uniqueId) const = 0;

            virtual int64_t getUniqueId() const = 0;

            /// @brief Get the parent node of this navigation node.
            /// Returns a NON-OWNING pointer. Do NOT release.
            /// @return Parent node pointer, or nullptr if this is the root node.
            virtual INavigationNode *getParent() const = 0;

            /// @brief Helper to check if this node is the root node.
            /// @return True if this node is the root node (i.e., has no parent), false otherwise.
            bool isRootNode() const
            {
                return getParent() == nullptr;
            }

            /// @brief Remove the object at the specified row index. May / may not be implemented by all nodes.
            /// @param rowIndex 
            virtual void removeObjectAtRow(RowIndex_t rowIndex) = 0;

            virtual const std::string &getName() const = 0;

            virtual const std::vector<DataColumn> &getColumns() const = 0;

            virtual bool getNumberOfRows(int64_t &outCount) const = 0;

            virtual const TrackQueryArgs *getQueryArgs() const = 0;

            virtual std::string getCellText(RowIndex_t rowIndex, ColumnIndex_t index) const = 0;
            virtual const TrackInfo *getTrackInfoForRow(RowIndex_t rowIndex) const = 0;

            /// @brief Prepare to show data for this node.
            /// This method is called when the node's data is about to be displayed.
            /// It can be used to perform any necessary setup or caching.
            virtual bool prepareToShowData() = 0;

            /// @brief Notify that the data is no longer being shown.
            /// This method is called when the node's data is no longer being displayed.
            /// It can be used to release resources or clear caches.
            virtual void dataNoLongerShowing() = 0;

            /// @brief Get the actions available for this node.
            virtual const DataActions &getNodeActions() const = 0;

            /// @brief Get the actions available for a specific row in this node.
            virtual const DataActions &getRowActions(RowIndex_t row) const = 0;

            virtual bool setSortOrder(const std::vector<SortOrderInfo> &sortOrders) = 0; // UI tells node how to sort
            virtual std::vector<SortOrderInfo> getCurrentSortOrder() const = 0;          // UI can query current sort

            virtual bool setSearchTerms(const std::vector<std::string> &searchTerms) = 0;
            virtual std::vector<std::string> getCurrentSearchTerms() const = 0;
        };

    } // namespace database
} // namespace jucyaudio
