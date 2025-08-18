#pragma once

#include <Database/Includes/TrackInfo.h>
#include <Database/Nodes/LibraryNode.h>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        class VirtualFolderNode : public LibraryNode
        {
        public:
            VirtualFolderNode(INavigationNode *parent, const FolderInfo &folderInfo);

            // Override INavigationNode interface
            bool canExpand() override;
            ObjectId getUniqueId() const override;
            bool expand(std::vector<INavigationNode *> &outChildren) override;
            bool getTotalTrackCount(int64_t &outCount) const override;

            int64_t getFolderId() const
            {
                return m_folderId;
            }

            // Override to check if this folder is online
            bool isOnline() const override;

        private:
            FolderId m_folderId;
            mutable bool m_onlineStatusCached{false};
            mutable bool m_isOnline{true};
        };

    } // namespace database
} // namespace jucyaudio