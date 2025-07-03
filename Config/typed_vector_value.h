#pragma once

#include <Config/value_interface.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace config
    {
        template <typename SectionType> class TypedValueVector : public ValueInterface
        {
        public:
            TypedValueVector(Section *parentSection, std::string keyName)
                : m_keyName{std::move(keyName)},
                  m_parentSection{parentSection}
            {
                spdlog::info("{}: creating TypedValueVector ({}, {}) at {}", __FUNCTION__, (const void*) parentSection, keyName, (const void *) this);

                if (parentSection)
                {
                    parentSection->addChildItem(this);
                }
            }

            ~TypedValueVector() override
            {
                spdlog::info("{}: destroying TypedValueVector ({}, {}) at {}", __FUNCTION__, (const void*) m_parentSection, m_keyName, (const void *) this);
            }

            // Access methods
            const std::vector<std::unique_ptr<SectionType>> &get() const
            {
                return m_items;
            }

            bool empty() const
            {
                return m_items.empty();
            }

            void clear()
            {
                m_items.clear();
            }

            void add(std::unique_ptr<SectionType> item)
            {
                m_items.push_back(std::move(item));
            }

            SectionType *addNew()
            {
                spdlog::info("{} Adding new item to vector: {}", __FUNCTION__, m_keyName);
                auto item = std::make_unique<SectionType>(nullptr, m_keyName + "/" + std::to_string(m_items.size()));
                SectionType *ptr = item.get();
                spdlog::info("{}: Created new item {} at path: {}", __FUNCTION__, (const void*) ptr, ptr->getConfigPath());
                m_items.push_back(std::move(item));
                spdlog::info("{}: Back item nowi s {}", __FUNCTION__, (const void*) m_items.back().get());
                return ptr;
            }

            size_t size() const
            {
                return m_items.size();
            }

            SectionType *operator[](size_t index)
            {
                return (index < m_items.size()) ? m_items[index].get() : nullptr;
            }

            const SectionType *operator[](size_t index) const
            {
                return (index < m_items.size()) ? m_items[index].get() : nullptr;
            }

            // ValueInterface implementation
            bool load(ConfigBackend &settings) override
            {
                // Clear existing items
                m_items.clear();

                // Discover how many items exist by probing
                int index = 0;
                while (settings.sectionExists(getConfigPath() + "/" + std::to_string(index)))
                {
                    // Create new item with indexed path - parent is the Section that owns this vector
                    auto item = std::make_unique<SectionType>(nullptr, getConfigPath() + "/" + std::to_string(index));

                    // Load the item's data
                    if (!item->load(settings))
                    {
                        // Log error but continue with other items
                        spdlog::error("Failed to load vector item at index {}: {}", index, item->getConfigPath());
                    }

                    m_items.push_back(std::move(item));
                    index++;
                }
                spdlog::info("Loaded {} items into vector: {}", index, getConfigPath());

                return true;
            }

            bool save(ConfigBackend &settings) const override
            {
                spdlog::info("{}: Saving {} items of vector: {}", __FUNCTION__, m_items.size(), getConfigPath());

                // 1. Determine old count by probing existing sections
                int oldCount = 0;
                while (settings.sectionExists(getConfigPath() + "/" + std::to_string(oldCount)))
                {
                    oldCount++;
                }

                // 2. Save current items (with updated indices)
                for (size_t i = 0; i < m_items.size(); ++i)
                {
                    // Update the item's section name to match current index
                    // Note: This assumes SectionType allows updating its group name

                    spdlog::info("{}: my path: {}, item path: {}", __FUNCTION__, getConfigPath(), m_items[i]->getConfigPath());
                    if (!m_items[i]->save(settings))
                    {
                        spdlog::error("{}: Failed to save vector item at index {}: {}", __FUNCTION__, i, m_items[i]->getConfigPath());
                        return false;
                    }
                }

                // 3. Clean up stale entries (if we have fewer items now)
                for (int i = static_cast<int>(m_items.size()); i < oldCount; ++i)
                {
                    if (!settings.deleteSection(getConfigPath() + "/" + std::to_string(i)))
                    {
                        spdlog::error("{}: Failed to delete stale vector item at index {}", __FUNCTION__, i);
                        // Continue cleanup even if one fails
                    }
                }

                return true;
            }

            std::string getConfigPath() const override
            {
                return m_parentSection->getConfigPath() + "/" + m_keyName;
            }

            void revertToDefault() override
            {
                // Clear all items and revert to empty vector
                m_items.clear();
            }

            void addChildItem(ValueInterface *item) override
            {
                // TypedValueVector manages its own children, ignore external additions
            }

        private:
            std::string m_keyName;
            Section *m_parentSection{nullptr};
            std::vector<std::unique_ptr<SectionType>> m_items;
        };
    } // namespace config
} // namespace jucyaudio
