#include <Database/Nodes/LogicalFolderNode.h>
#include <Utils/AssortedUtils.h>
#include <Utils/UiUtils.h>
#include <algorithm>
#include <cassert>
#include <cctype>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace database
    {

        LogicalFolderNode::LogicalFolderNode(INavigationNode *parent, const std::filesystem::path &folderPath,
                                             const std::string &displayName)
            : LibraryNode{parent, displayName}, // Call base constructor
              m_thisFolderPath{folderPath}
        {
            // IMPORTANT: Initialize this instance's query to be specific to its path
            m_queryArgs.pathFilter = m_thisFolderPath;
            // BaseNode part (if LibraryNode inherits from BaseNode for name/id) needs its name set.
            // If BaseNode is a direct base of LibraryNode:
            // this->m_name = displayName; // If m_name is protected in BaseNode and not const
            // Or LibraryNode's constructor needs to pass name to BaseNode's constructor.
            // This detail depends on how BaseNode/LibraryNode constructors are set up.
            // The main point is this LogicalFolderNode instance must configure its inherited
            // m_queryArgs to filter by its own path.
        }

        bool LogicalFolderNode::hasChildren() const
        {
            // Use std::filesystem to check for the existence of any subdirectories
            // This avoids iterating through all entries if we just need to know if AT LEAST ONE exists.
            std::error_code ec; // To catch potential errors without throwing exceptions
            for (const auto &entry : std::filesystem::directory_iterator(m_thisFolderPath, ec))
            {
                if (ec)
                {
                    // Handle error, e.g., path doesn't exist, permissions issue
                    spdlog::warn("LogicalFolderNode::hasChildren - Error iterating directory {}: {}", m_thisFolderPath.string(), ec.message());
                    return false; // No accessible children or error
                }
                if (entry.is_directory(ec))
                {
                    if (ec)
                    { // Error checking is_directory
                        spdlog::warn("LogicalFolderNode::hasChildren - Error checking entry type for {}: {}", entry.path().string(), ec.message());
                        continue; // Skip this problematic entry
                    }
                    return true; // Found at least one subdirectory
                }
            }
            if (ec)
            { // Check for error if the loop itself didn't run (e.g. path not found initially)
                spdlog::warn("LogicalFolderNode::hasChildren - Initial error iterating directory {}: {}", m_thisFolderPath.string(), ec.message());
            }
            return false; // No subdirectories found or error occurred
        }

        bool LogicalFolderNode::getChildren(std::vector<INavigationNode *> &outChildren)
        {
            assert(outChildren.empty()); // Precondition

            std::error_code ec;
            try
            {
                // Check if the path exists and is a directory before iterating
                if (!std::filesystem::exists(m_thisFolderPath, ec) || !std::filesystem::is_directory(m_thisFolderPath, ec))
                {
                    if (ec)
                    {
                        spdlog::warn("LogicalFolderNode::getChildren - Filesystem error accessing path {}: {}", m_thisFolderPath.string(), ec.message());
                    }
                    else
                    {
                        spdlog::warn("LogicalFolderNode::getChildren - Path {} is not a directory or does not exist.", m_thisFolderPath.string());
                    }
                    return false; // Cannot get children if path is invalid
                }

                for (const auto &entry : std::filesystem::directory_iterator(m_thisFolderPath, ec))
                {
                    if (ec)
                    { // Error during iteration
                        spdlog::warn("LogicalFolderNode::getChildren - Error iterating directory {}: {}", m_thisFolderPath.string(), ec.message());
                        // Decide whether to stop or skip this entry. For robustness, maybe stop.
                        return false; // Or clear outChildren and return false
                    }

                    if (entry.is_directory(ec))
                    {
                        if (ec)
                        { // Error checking entry type
                            spdlog::warn("LogicalFolderNode::getChildren - Error checking entry type for {}: {}", entry.path().string(), ec.message());
                            continue; // Skip this problematic entry
                        }

                        // Create new LogicalFolderNode for each sub-directory
                        auto *childFolderNode = new LogicalFolderNode{
                            this,                      // Parent node
                            entry.path(),              // Full path of the subdirectory
                            pathToString(entry.path().filename()) // Display name (just the folder name)
                                                       // u8string() then convert to std::string as needed
                        };
                        outChildren.push_back(childFolderNode);
                    }
                    // We are only interested in directories as children for this node type
                }
                if (ec)
                { // Check for error if the loop didn't run (e.g. path not found initially before check above)
                    spdlog::warn("LogicalFolderNode::getChildren - Initial error iterating directory {}: {}", m_thisFolderPath.string(), ec.message());
                    return false;
                }

                // Sort children by name (case-insensitive, Unicode-aware)
                std::sort(outChildren.begin(), outChildren.end(), 
                    [](const INavigationNode* a, const INavigationNode* b) {
                        std::string nameA = a->getName();
                        std::string nameB = b->getName();
                        
                        // Convert to lowercase for case-insensitive comparison
                        std::transform(nameA.begin(), nameA.end(), nameA.begin(), 
                            [](unsigned char c) { return std::tolower(c); });
                        std::transform(nameB.begin(), nameB.end(), nameB.begin(), 
                            [](unsigned char c) { return std::tolower(c); });
                        
                        return nameA < nameB;
                    });
            }
            catch (const std::filesystem::filesystem_error &e)
            {
                // Catch other potential filesystem exceptions
                spdlog::error("LogicalFolderNode::getChildren - Filesystem exception for path {}: {}", m_thisFolderPath.string(), e.what());
                return false;
            }

            return !outChildren.empty();
        }

        void LogicalFolderNode::createChildren(INavigationNode *parent, std::vector<INavigationNode *> &children)
        {
            std::vector<FolderInfo> folders;
            if (theTrackLibrary.getFolderDatabase().getFolders(folders))
            {
                for (const auto &folder : folders)
                {
                    children.emplace_back(new LogicalFolderNode{parent, folder.path, pathToString(folder.path.filename())});
                }
            }
        }

    } // namespace database
} // namespace jucyaudio
