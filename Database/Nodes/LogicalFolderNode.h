#pragma once

#include <Database/Includes/FolderInfo.h>
#include <Database/Nodes/LibraryNode.h>

namespace jucyaudio
{
    namespace database
    {
        /**
         * @brief Represents a single folder from the database in the navigation tree.
         * This node's children are other LogicalFolderNodes, creating a hierarchy that
         * mirrors the structure in the `Folders` table.
         */
        class LogicalFolderNode : public LibraryNode
        {
        public:
            /**
             * @brief Constructs a node representing a database folder.
             * @param parent The parent node in the tree.
             * @param folderInfo The data for this folder, retrieved from the database.
             */
            LogicalFolderNode(INavigationNode *parent, const FolderInfo &folderInfo);
            ~LogicalFolderNode() override = default;

            bool canExpand() override;
            bool expand(std::vector<INavigationNode *> &outChildren) override;

            /**
             * @brief A static factory method to create the initial set of root-level folder nodes.
             * @param parent The root node of the entire navigation tree.
             * @param children A vector to be populated with the created root folder nodes.
             */
            static void createRootFolderNodes(INavigationNode *parent, std::vector<INavigationNode *> &children);

            /// @brief Returns the database ID of the folder this node represents.
            FolderId getFolderId() const
            {
                return m_folderId;
            }

        private:
            FolderId m_folderId;
        };
    } // namespace database
} // namespace jucyaudio