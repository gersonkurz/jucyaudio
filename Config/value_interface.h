#pragma once

// @file jucyaudio/Config/value_interface.h
// @brief defines what a configuration value does

#include <Config/config_backend.h> // Assuming this is the correct path to ConfigBackend
#include <string>

namespace jucyaudio
{
    namespace config
    {
        /// @brief Interface for configuration values.
        /// It's really pretty abstract, but it is also the core class for the tree of configuration 
        /// values. At its most abstract, it is something that loads / saves from a backend.
        class ValueInterface
        {
        public:
            virtual ~ValueInterface() = default;

            /// @brief Get the value from the backend, whatever it is. 
            /// @param settings The backend to load from
            /// @return true if the value was loaded successfully, false otherwise.
            virtual bool load(ConfigBackend &settings) = 0;

            /// @brief Save the value to the backend, whatever it is.
            /// @param settings The backend to save to
            /// @return true if the value was saved successfully, false otherwise.
            virtual bool save(ConfigBackend &settings) const = 0;

            /// @brief This is needed only for sections to hold their parent values;
            ///        `TypedValue<T>` will *NOT* use this, only `Section` will.
            /// @param item The child item to add
            virtual void addChildItem(ValueInterface *item) = 0;

            /// @brief Revert to the default value.
            virtual void revertToDefault() = 0;

            /// @brief  Get the configuration path for this value
            /// @return A string with a path, e.g. some/section/key
            virtual std::string getConfigPath() const = 0;
        };

    } // namespace config
} // namespace jucyaudio
