#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <toml++/toml.h>
#include <unordered_map>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jucyaudio
{
    namespace ui
    {
        struct JucyTheme
        {
            std::string name;
            std::unordered_map<int, juce::Colour> colours;
        };

        class ThemeManager final
        {
        public:
            ThemeManager() = default;
            ~ThemeManager() = default;

            // Loads a single theme file into our JucyTheme struct.
            std::optional<JucyTheme> loadThemeFromFile(const std::filesystem::path &path);
            void scanThemesDirectory(const std::filesystem::path &themesFolderPath);
            void applyTheme(juce::LookAndFeel_V4& lookAndFeel, size_t themeIndex, juce::Component* pComponent);
            const auto& getAvailableThemes() const
            {
                return m_availableThemes;
            }

        private:
            std::vector<JucyTheme> m_availableThemes;
        };

        extern ThemeManager theThemeManager;
    } // namespace ui
} // namespace jucyaudio