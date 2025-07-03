#include <Config/section.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace config
    {

        // Root section constructor
        Section::Section()
            : m_parent{nullptr}
        {
            // Root section has no parent to register with
            spdlog::info("{}: root section created at {}", __FUNCTION__, (const void *) this);
        }

        // Child section constructor
        Section::Section(Section *parent, std::string groupName)
            : m_parent{parent},
              m_groupName{std::move(groupName)}

        {
            spdlog::info("{}: creating Section ({}, {}) at {}", __FUNCTION__, (const void*) parent, groupName, (const void *) this);
            if (parent)
            {
                parent->addChildItem(this);
            }
        }

        void Section::addChildItem(ValueInterface *item)
        {
            spdlog::info("{}: addChildItem {} to {}", __FUNCTION__, (const void*) item, (const void *) this);

            m_childItems.push_back(item);
        }

        std::string Section::getConfigPath() const
        {
            if (!m_parent)
            {
                spdlog::info("{} for {} returns group name {} because there is no parent", __FUNCTION__, (const void*) this, m_groupName);
                return m_groupName;
            }

            const auto parentPath{m_parent->getConfigPath()};
            spdlog::info("{} for {} gets parent path {}", __FUNCTION__, (const void*) this, parentPath);
            if (parentPath.empty())
            {
                spdlog::info("{} for {} returns group name {} because parent path is empty", __FUNCTION__, (const void*) this, m_groupName);
                return m_groupName;
            }

            const auto result = parentPath + "/" + m_groupName;
            spdlog::info("{} for {} returns result {} as combination", __FUNCTION__, (const void*) this, result);
            return result;
        }

        bool Section::load(ConfigBackend &settings)
        {
            spdlog::info("{}: loading section {} at path {}", __FUNCTION__, (const void*) this, getConfigPath());
            bool success = true;
            for (const auto item : m_childItems)
            {
                spdlog::info("{}: loading item {}", __FUNCTION__, (const void*) item);
                if (!item->load(settings))
                {
                    spdlog::error("{} Failed to load config item: {}", __FUNCTION__, item->getConfigPath());
                    success = false;
                }
                spdlog::info("{}: loaded item {} at path {}", __FUNCTION__, (const void*) item, item->getConfigPath());
            }
            spdlog::info("{}: complete loading section {} at path {} with {}", __FUNCTION__, (const void*) this, getConfigPath(), success ? "success" : "failure");
            return success;
        }

        bool Section::save(ConfigBackend &settings) const
        {
            bool success = true;
            for (const auto item : m_childItems)
            {
                if (!item->save(settings))
                {
                    spdlog::error("Failed to save config item: {}", item->getConfigPath());
                    success = false;
                }
            }
            return success;
        }

        void Section::revertToDefault()
        {
            for (const auto item : m_childItems)
            {
                item->revertToDefault();
            }
        }

    } // namespace config
} // namespace jucyaudio
