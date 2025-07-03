#include <Database/Includes/INavigationNode.h>
#include <Database/Nodes/RootNode.h>
#include <UI/Settings.h>


namespace jucyaudio
{
    // you used a different name here - but why? this makes the code so much harder
    // to read with the constant namespace re-declaration?
    namespace config
    {
        RootSettings theSettings; // Global instance

        using namespace jucyaudio::database;
        TypedValueVector<DataViewColumnSection> *getSectionFor(INavigationNode *node)
        {
            if (node)
            {
                const auto currentNodeName = node->getName();
                if (currentNodeName.starts_with(getWorkingSetsRootNodeName()))
                {
                    return &config::theSettings.uiSettings.workingSetsViewColumns;
                }
                else if (currentNodeName.starts_with(getFoldersRootNodeName()))
                {
                    return &config::theSettings.uiSettings.foldersViewColumns;
                }
                else if (currentNodeName.starts_with(getMixesRootNodeName()))
                {
                    return &config::theSettings.uiSettings.mixesViewColumns;
                }
                else if (currentNodeName.starts_with(getLibraryRootNodeName()))
                {
                    return &config::theSettings.uiSettings.libraryViewColumns;
                }
            }
            return nullptr;
        }

    } // namespace config

} // namespace jucyaudio
