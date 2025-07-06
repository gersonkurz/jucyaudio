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
            
            void initialize(const std::filesystem::path &themesFolderPath, const std::string& currentThemeName);

            void applyCurrentTheme(juce::LookAndFeel_V4& lookAndFeel, juce::Component* pComponent)
            {
                applyTheme(lookAndFeel, m_currentThemeIndex, pComponent);
            }

            bool isCurrentIndex(size_t index) const
            {
                return index == m_currentThemeIndex;
            }

            std::string applyTheme(juce::LookAndFeel_V4& lookAndFeel, size_t themeIndex, juce::Component* pComponent);

            const auto& getAvailableThemes() const
            {
                return m_availableThemes;
            }

        private:
            std::optional<JucyTheme> loadThemeFromFile(const std::filesystem::path &path);

            std::string getNameOfTheme(size_t themeIndex) const
            {
                if (themeIndex < m_availableThemes.size())
                {
                    return m_availableThemes[themeIndex].name;
                }
                return {};
            }

            size_t getThemeIndexByName(const std::string &themeName) const
            {
                for (size_t i = 0; i < m_availableThemes.size(); ++i)
                {
                    if (m_availableThemes[i].name == themeName)
                    {
                        return static_cast<int>(i);
                    }
                }
                return 0; // Not found => revert to default theme
            }

            std::vector<JucyTheme> m_availableThemes;
            const size_t INVALID_THEME_INDEX = static_cast<size_t>(-1); // Sentinel value for invalid index
            size_t m_currentThemeIndex{INVALID_THEME_INDEX}; // Index of the currently applied theme (initial: not registered at all)
        };

        extern ThemeManager theThemeManager;
    } // namespace ui
} // namespace jucyaudio