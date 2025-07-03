// Engine/RootNode.h
#pragma once

#include <Database/Includes/INavigationNode.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/Nodes/BaseNode.h>
#include <Database/TrackLibrary.h>
#include <algorithm> // For std::generate_n
#include <atomic>    // For unique ID generation
#include <random>    // For randomized data
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        constexpr auto getWorkingSetsRootNodeName()
        {
            return "Working Sets";
        }
        
        constexpr auto getFoldersRootNodeName()
        {
            return "Folders";
        }

        constexpr auto getMixesRootNodeName()
        {
            return "Mixes";
        }

        constexpr auto getLibraryRootNodeName()
        {
            return "Library";
        }

        class RootNode final : public BaseNode
        {
        public:
            RootNode(TrackLibrary &library);

            INavigationNode *get(const std::string &name) const override;

            auto getWorkingSetsRootNode() const
            {
                return get(getWorkingSetsRootNodeName());
            }

            auto getFoldersRootNode() const
            {
                return get(getFoldersRootNodeName());
            }

            auto getMixesRootNode() const
            {
                return get(getMixesRootNodeName());
            }

        private:
            // INavigationNode interface
            bool getChildren(std::vector<INavigationNode *> &outChildren) override;
            bool hasChildren() const override;
        };

    } // namespace database
} // namespace jucyaudio
