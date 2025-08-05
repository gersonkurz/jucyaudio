#pragma once

#include <Database/Includes/LibraryRootInfo.h>
#include <optional>
#include <string_view>
#include <vector>

namespace jucyaudio
{
    namespace database
    {
        /**
         * @brief Manages the user-defined library root folders in the database.
         */
        class ILibraryRootManager
        {
        public:
            virtual ~ILibraryRootManager() = default;

            /**
             * @brief Retrieves all configured library roots.
             * @return A vector of LibraryRootInfo objects.
             */
            virtual std::vector<LibraryRootInfo> getAllRoots() const = 0;

            /**
             * @brief Adds a new library root path to the database.
             * @param path The filesystem path to add as a root.
             * @return The created LibraryRootInfo including its new ID, or std::nullopt if it failed (e.g., path already exists).
             */
            virtual std::optional<LibraryRootInfo> addRoot(std::string_view path) = 0;

            /**
             * @brief Updates the path of an existing library root.
             * @param rootId The ID of the library root to update.
             * @param newPath The new filesystem path.
             * @return true if successful, false otherwise.
             */
            virtual bool updateRootPath(LibraryRootId rootId, std::string_view newPath) = 0;

            /**
             * @brief Removes a library root from the database.
             * @param rootId The ID of the library root to remove.
             * @return true if successful, false otherwise.
             */
            virtual bool removeRoot(LibraryRootId rootId) = 0;
        };

    } // namespace database
} // namespace jucyaudio