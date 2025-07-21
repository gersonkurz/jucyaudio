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
            bool hasChildren() const override;
            bool getChildren(std::vector<INavigationNode*>& outChildren) override;
            
            // Override prepareToShowData to set our folder filter
            bool prepareToShowData() override;
            
        private:
            int64_t m_folderId;
        };
        
    } // namespace database
} // namespace jucyaudio