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

namespace jucyaudio
{
    namespace database
    {
        struct DataColumn
        {
            /// @brief Column index type, starting from 0.
            ColumnIndex_t index{0}; // Column index, starting from 0

            /// @brief SQL-id for this column
            std::string sqlId;

            /// @brief Unique identifier for the column. This should be unique across all columns in a navigation node.
            std::string name;

            /// @brief Default display width. Not sure this is needed, probably the UI could decide upon that on its own?
            int32_t defaultWidth{100};

            /// @brief Alignment of the column in the UI. Not sure this is needed - because we also have the DataTypeHint, and the UI
            /// can use that to determine how to align the text.
            ColumnAlignment alignment{ColumnAlignment::Left};

            /// @brief Type hint for the column data. Needed mostly for sorting and potentially UI formatting but see DatValue belo
            ColumnDataTypeHint typeHint{ColumnDataTypeHint::String};
        };
        
        struct IRefCounted
        {
            IRefCounted() = default; // Default constructor
            virtual ~IRefCounted() = default;

            // Note: We use deleted move constructors/assignment operators to allow moving but prevent copying.
            IRefCounted(const IRefCounted &) = delete;            // No copy
            IRefCounted &operator=(const IRefCounted &) = delete; // No assignment
            IRefCounted(IRefCounted &&) = default;                // Move is fine
            IRefCounted &operator=(IRefCounted &&) = default;     // Move assignment is fine

            /// @brief Increment the reference count.
            /// This method should be called when a new reference to the object is created.
            /// It is expected to be thread-safe.
            virtual void retain(REFCOUNT_DEBUG_SPEC) const = 0;

            /// @brief Decrement the reference count.
            /// This method should be called when a reference to the object is no longer needed.
            /// If the reference count reaches zero, the object should be deleted.
            /// It is expected to be thread-safe.
            /// @note After calling this method, the object should not be used anymore.
            /// @note If the object is deleted, it should not call any methods on itself after this point.
            virtual void release(REFCOUNT_DEBUG_SPEC) const = 0;

            /// @brief Clear the internal state of the object.
            /// This method is intended to reset the internal state of the object without deleting it.
            /// It can be used to free resources or reset data. It does NOT touch the reference count.
            virtual void clear() = 0;
        };

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
