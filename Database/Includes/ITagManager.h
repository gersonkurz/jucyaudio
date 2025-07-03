#pragma once

#include <optional>
#include <string>
#include <vector>
#include <Database/Includes/Constants.h>


namespace jucyaudio
{
    namespace database
    {
        class ITagManager
        {
        public:
            virtual ~ITagManager() = default;

            // --- Core Tag Operations ---

            /**
             * @brief Gets the ID for a given tag name. If the tag doesn't exist and createIfMissing is true,
             *        it will be created.
             * @param tagName The name of the tag. Can also be a string-representation of the TagId. Case-insensitive matching is preferred for lookup.
             * @param createIfMissing If true, creates the tag in the database if it doesn't exist.
             * @return The TagId if found or created, or std::nullopt if not found and createIfMissing is false,
             *         or if an error occurred during creation.
             */
            virtual std::optional<TagId> getOrCreateTagId(const std::string &tagName, bool createIfMissing = true) = 0;

            /**
             * @brief Gets the name for a given TagId.
             * @param tagId The ID of the tag.
             * @return The tag name, or std::nullopt if not found or error.
             *         (Consider if this should return std::string and throw/log on error, or return optional for not found)
             */
            virtual std::optional<std::string> getTagNameById(TagId tagId) const = 0;

            /**
             * @brief Gets all tags, possibly filtered by a search term.
             * @param nameFilter Optional filter to apply to tag names (e.g., for autocomplete).
             * @return A vector of (TagId, TagName) pairs or just TagName strings.
             *         Let's go with pairs for more flexibility.
             */

            virtual std::vector<TagInfo> getAllTags(const std::optional<std::string> &nameFilter = std::nullopt) const = 0;
        };

    } // namespace database
} // namespace jucyaudio
