#pragma once

#include <Database/Nodes/LibraryNode.h>
#include <Database/Includes/TrackInfo.h>
#include <vector>
#include <string>

namespace jucyaudio
{
    namespace database
    {
        class VirtualFolderNode : public LibraryNode
        {
        public:
            VirtualFolderNode(INavigationNode* parent, int64_t folderId, const std::string& folderName);
            
            // Override INavigationNode interface
            bool canExpand() override;
            bool expand(std::vector<INavigationNode *> &outChildren) override;
            
            // Override prepareToShowData to set our folder filter
            bool prepareToShowData() override;
            
            // Override getTotalTrackCount to return recursive count
            bool getTotalTrackCount(int64_t &outCount) const override;
            
            int64_t getFolderId() const { return m_folderId; }
            
        private:
            int64_t m_folderId;
        };
        
    } // namespace database
} // namespace jucyaudio