// LogicalFolderNode.h (Conceptual)
#include <Database/Nodes/LibraryNode.h>
#include <filesystem>

namespace jucyaudio
{
    namespace database
    {
        class LogicalFolderNode : public LibraryNode
        {
        public:
            LogicalFolderNode(INavigationNode *parent, TrackLibrary &library, const std::filesystem::path &folderPath,
                              const std::string &displayName); // displayName might be folderPath.filename()
            ~LogicalFolderNode() override = default;

            bool hasChildren() const override;
            bool getChildren(std::vector<INavigationNode *> &outChildren) override;

            static void createChildren(INavigationNode *parent, TrackLibrary &library, std::vector<INavigationNode *> &children);

        private:
            std::filesystem::path m_thisFolderPath;
            // It inherits m_library, m_queryArgs, m_tracks from LibraryNode.
            // Each instance will have its own copies of these.
        };
    }
}
