#pragma once

#include <Config/value_interface.h>
#include <memory>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace config
    {
        /// @brief A Section is a group of configuration items, which can be
        /// nested. It can contain both TypedValue items and other Sections.
        class Section : public ValueInterface
        { // A Section is also a ValueInterface for nesting
        public:
            explicit Section();

            // Constructor for child section
            Section(Section *parent, std::string groupNameAsKey);

            const std::string &getGroupName() const
            {
                return m_groupName;
            }

            bool load(ConfigBackend &settings) override;
            bool save(ConfigBackend &settings) const override;

            std::string getConfigPath() const override;

            void revertToDefault() override;

            const auto &getChildItems() const
            {
                return m_childItems;
            }

            // Called by TypedValue<T> and nested Section constructors
            void addChildItem(ValueInterface *item) override;

        private:
            Section *m_parent{nullptr};
            std::string m_groupName;
            std::vector<ValueInterface *> m_childItems; // Pointers to members (TypedValue or sub-Section)
        };

    } // namespace config
} // namespace jucyaudio
