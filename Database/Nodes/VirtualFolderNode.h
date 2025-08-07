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
            bool expand(std::vector<INavigationNode *> &outChildren) override;
            bool getTotalTrackCount(int64_t &outCount) const override;

            int64_t getFolderId() const
            {
                return m_folderId;
            }

        private:
            FolderId m_folderId;
        };

    } // namespace database
} // namespace jucyaudio