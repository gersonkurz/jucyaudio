#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <Database/Includes/Constants.h>
#include <Database/Includes/DataColumn.h>
#include <Database/Includes/IRefCounted.h>
#include <Database/Includes/TrackQueryArgs.h>

namespace jucyaudio
{
    namespace database
    {
        // --- INavigationNode Interface (remains an interface) ---
        struct INavigationNode : public IRefCounted
        {

            virtual ~INavigationNode() = default;

            // --------------------- REDESIGNED METHODS -------------------------------------
            explicit INavigationNode(std::string_view typeNameForSingleObject, std::string_view typeNameForMultipleObjects)
                : m_refTypeNameForSingleObject{typeNameForSingleObject},
                  m_refTypeNameForMultipleObjects{typeNameForMultipleObjects}
            {
            }


            // @brief Get the name of this navigation node. This is different from the object name:
            // for example, a specific mix might be named "My Awesome Mix", but the object class still is "mix".
            virtual const std::string &getName() const = 0;

            // @brief Check if this node can be expanded.
            // @note: This is not declared as 'const' because concievably it invalidates some cache
            // @note: You could just call expand() and then check if the result is empty - but by having
            // a separate method, we can possibly avoid the overhead of actually expanding
            virtual bool canExpand() = 0;

            // @brief Expand this node and retrieve its children.
            // This method populates the provided vector with the expanded nodes.
            // @param children Reference to a vector that will be populated with expanded nodes.
            // &return True if the expansion was successful and children were added,
            /// @note The children nodes are retained by this method, so the caller must release them when done.
            virtual bool expand(std::vector<INavigationNode *> &children) = 0;

            // @brief Collapse this node, removing its children from the provided vector.
            // This method clears the children vector, effectively collapsing the node.
            virtual bool collapse() = 0;

            // @brief Get the actions available for this node.
            // This method returns the actions that can be performed on this node
            // itself, such as creating a new mix, working set, etc.
            /// @return A reference to a DataActions object containing the available actions.
            virtual const DataActions &getNodeActions() const = 0;

            // @brief Remove tracks from the underlying data source (typically, a working-set or a mix)
            // @param trackIds The IDs of the tracks to remove.
            // @return True if the tracks were successfully removed, false otherwise.
            virtual bool removeObjects(const std::vector<ObjectId>& objectIds) const = 0;

            // @brief Delete this object from the underlying data source.
            // This method is called when the user wants to delete this object (e.g., a mix, working set, etc.).
            // It should handle the deletion logic and return true if the object was successfully deleted, 
            // or false if the deletion failed (e.g., due to constraints or errors).
            /// @return True if the object was successfully deleted, false otherwise.
            virtual bool deleteThisObject() = 0;

            // @brief Notify that a child node has been deleted. It should update its internal state accordingly.
            virtual void nodeHasBeenDeleted(INavigationNode *node) = 0;

            // @brief Rename this node
            virtual void rename(std::string_view newName) = 0;

            // @brief The name for a single object of this type. It's not a function, because implementing it
            // in every type of node is cumbersome, and the name is constant anyway, so I simply
            // added it to the (new) constructor.
            // @example: "Track", "Mix", "Working Set", etc.
            const std::string_view m_refTypeNameForSingleObject;

            // @brief The name for multiple objects of this type. It's not a function, because implementing it
            // in every type of node is cumbersome, and the name is constant anyway, so I simply
            // added it to the (new) constructor.
            // @example: "Tracks", "Mixes", "Working Sets", etc.
            const std::string_view m_refTypeNameForMultipleObjects;

            // --------------------- METHODS IN NEED FOR REVIEW -----------------------------

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

            virtual ObjectId getUniqueId() const = 0;

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

            virtual const std::vector<DataColumn> &getColumns() const = 0;

            virtual bool getNumberOfRows(int64_t &outCount) const = 0;

            /// @brief Get the total track count including all descendant nodes.
            /// For folder nodes, this returns the recursive count of all tracks in subfolders.
            /// For other nodes, this typically returns the same value as getNumberOfRows().
            /// @param outCount Reference to store the total track count.
            /// @return True if the count was successfully retrieved, false otherwise.
            virtual bool getTotalTrackCount(int64_t &outCount) const = 0;

            virtual const TrackQueryArgs *getQueryArgs() const = 0;

            virtual std::string getCellText(RowIndex_t rowIndex, ColumnIndex_t index) const = 0;
            virtual const TrackInfo *getTrackInfoForRow(RowIndex_t rowIndex) const = 0;
            virtual int64_t getObjectIdForRow(RowIndex_t rowIndex) const = 0;

            /// @brief Prepare to show data for this node.
            /// This method is called when the node's data is about to be displayed.
            /// It can be used to perform any necessary setup or caching.
            virtual bool prepareToShowData() = 0;

            /// @brief Notify that the data is no longer being shown.
            /// This method is called when the node's data is no longer being displayed.
            /// It can be used to release resources or clear caches.
            virtual void dataNoLongerShowing() = 0;


            /// @brief Get the actions available for a specific row in this node.
            virtual const DataActions &getRowActions(RowIndex_t row) const = 0;

            virtual bool setSortOrder(const std::vector<SortOrderInfo> &sortOrders) = 0; // UI tells node how to sort
            virtual std::vector<SortOrderInfo> getCurrentSortOrder() const = 0;          // UI can query current sort

            virtual bool setSearchTerms(const std::vector<std::string> &searchTerms) = 0;
            virtual std::vector<std::string> getCurrentSearchTerms() const = 0;

            /// @brief Get all track IDs for the current node's query.
            /// This method retrieves all track IDs that match the current filter, sort, and other query parameters for the node.
            /// It is used for operations that need to act on the entire set of tracks represented by the node,
            /// such as running a batch analysis.
            /// @return A vector of TrackIds.
            virtual std::vector<TrackId> getAllTrackIds() const = 0;

            /// @brief Get the working set ID associated with this node. Will be 0 for all non-working-sets
            virtual WorkingSetId getWorkingSetId() const = 0;
        };

    } // namespace database
} // namespace jucyaudio
