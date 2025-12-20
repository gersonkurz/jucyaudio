#include <Config/section.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace config
    {
        std::shared_ptr<spdlog::logger> logger = spdlog::get("conf");

        // Root section constructor
        Section::Section()
            : m_parent{nullptr}
        {
            // Root section has no parent to register with
            if(logger)
            {
                logger->debug("{}: root section created at {}", __FUNCTION__, (const void *) this);
            }
        }

        // Child section constructor
        Section::Section(Section *parent, std::string groupName)
            : m_parent{parent},
              m_groupName{std::move(groupName)}

        {
            if(logger)
            {
                logger->debug("{}: creating Section ({}, {}) at {}", __FUNCTION__, (const void*) parent, groupName, (const void *) this);
            }
            if (parent)
            {
                parent->addChildItem(this);
            }
        }

        void Section::addChildItem(ValueInterface *item)
        {
            if(logger)
            logger->debug("{}: addChildItem {} to {}", __FUNCTION__, (const void*) item, (const void *) this);

            m_childItems.push_back(item);
        }

        std::string Section::getConfigPath() const
        {
            if (!m_parent)
            {
                logger->debug("{} for {} returns group name {} because there is no parent", __FUNCTION__, (const void*) this, m_groupName);
                return m_groupName;
            }

            const auto parentPath{m_parent->getConfigPath()};
            logger->debug("{} for {} gets parent path {}", __FUNCTION__, (const void*) this, parentPath);
            if (parentPath.empty())
            {
                logger->debug("{} for {} returns group name {} because parent path is empty", __FUNCTION__, (const void*) this, m_groupName);
                return m_groupName;
            }

            const auto result = parentPath + "/" + m_groupName;
            logger->debug("{} for {} returns result {} as combination", __FUNCTION__, (const void*) this, result);
            return result;
        }

        bool Section::load(ConfigBackend &settings)
        {
            logger->debug("{}: loading section {} at path {}", __FUNCTION__, (const void*) this, getConfigPath());
            bool success = true;
            for (const auto item : m_childItems)
            {
                logger->debug("{}: loading item {}", __FUNCTION__, (const void*) item);
                if (!item->load(settings))
                {
                    logger->error("{} Failed to load config item: {}", __FUNCTION__, item->getConfigPath());
                    success = false;
                }
                logger->debug("{}: loaded item {} at path {}", __FUNCTION__, (const void*) item, item->getConfigPath());
            }
            logger->debug("{}: complete loading section {} at path {} with {}", __FUNCTION__, (const void*) this, getConfigPath(), success ? "success" : "failure");
            return success;
        }

        bool Section::save(ConfigBackend &settings) const
        {
            bool success = true;
            for (const auto item : m_childItems)
            {
                if (!item->save(settings))
                {
                    logger->error("Failed to save config item: {}", item->getConfigPath());
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
