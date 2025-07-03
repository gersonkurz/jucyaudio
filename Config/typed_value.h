#pragma once

#include <Config/value_interface.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace config
    {
        extern std::shared_ptr<spdlog::logger> logger;
        
        //static auto logger = spdlog::get("conf");
        /// @brief Template for holding actual data values in the configuration system.
        /// @tparam T The type of the value to hold, e.g. int, bool, std::string.
        /// @note This class is a leaf in the configuration tree, meaning it does not have child items
        template <typename T> class TypedValue : public ValueInterface
        {
        public:
            /// @brief Constructor for TypedValue.
            /// @param parentSection Every value must have a parent section, which is
            ///                      a Section that holds the configuration path.
            /// @param keyName Name of this value in the configuration path (the leaf name)
            /// @param defaultValue Default value for the underlying data
            TypedValue<T>(ValueInterface *parentSection, std::string keyName, T defaultValue = {})
                : m_keyName{std::move(keyName)},
                  m_parentSection{parentSection},
                  m_defaultValue{std::move(defaultValue)},
                  m_value{m_defaultValue}
            {
                if(logger)
                logger->info("{}: creating TypedValue ({}, {}) at {}", __FUNCTION__, (const void*) parentSection, keyName, (const void *) this);
                if (parentSection)
                {
                    // Forward to a method in Section that knows about
                    // ValueInterface (Defined after Section is fully defined)
                    parentSection->addChildItem(this);
                }
            }

            ~TypedValue() override
            {
                logger->info("{}: destroying TypedValue ({}, {}) at {}", __FUNCTION__, (const void*) m_parentSection, m_keyName, (const void *) this);
            }

            /// @brief Accessor for the value
            /// @note Yes, I know, we also have operator overloading for const T &,
            ///       but this is more explicit and clear.
            /// @return Reference to the value stored in this TypedValue
            const T &get() const
            {
                return m_value;
            }

            /// @brief Assign a new value to this TypedValue.
            /// @note This will not save the value to the backend, you must call
            ///       `save()` on the ConfigBackend to persist the change.
            /// @param val new value to set
            void set(const T &val)
            {
                m_value = val;
            }

            /// @brief Implicit conversion operator to the stored type.
            /// @return A const reference to the stored value
            operator const T &() const
            {
                return m_value;
            }

            bool load(ConfigBackend &settings) override
            {
                return settings.load(getConfigPath(), m_value);
            }

            bool save(ConfigBackend &settings) const override
            {
                return settings.save(getConfigPath(), m_value);
            }

            /// @brief The path is actually relative to the parent section,
            ///        so we construct it by appending the key name to the parent's path.
            /// @return new path for this TypedValue
            std::string getConfigPath() const override
            {
                return m_parentSection->getConfigPath() + "/" + m_keyName;
            }

            void revertToDefault() override
            {
                m_value = m_defaultValue;
            }

            void addChildItem([[maybe_unused]] ValueInterface *item) override
            {
                // TypedValue does not support adding child items, so we ignore
                // this call. This is a no-op to satisfy the interface.
            }

        private:
            std::string m_keyName; // Leaf key name for QSettings
            const ValueInterface *m_parentSection{nullptr};
            T m_defaultValue;
            T m_value;
        };

    } // namespace config
} // namespace jucyaudio
