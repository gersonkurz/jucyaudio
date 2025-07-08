#include <UI/ThemeManager.h>
#include <spdlog/spdlog.h>
#include <toml++/toml.h> // Include the parser implementation here

namespace jucyaudio
{
    namespace ui
    {
        namespace
        {
#define DECLARE_COLOUR_ID(name) {#name, juce::name},
            const std::unordered_map<std::string, int> colourNameMap
            {
                DECLARE_COLOUR_ID(TreeView::backgroundColourId)
                DECLARE_COLOUR_ID(TreeView::linesColourId)
                DECLARE_COLOUR_ID(TreeView::dragAndDropIndicatorColourId)
                DECLARE_COLOUR_ID(TreeView::selectedItemBackgroundColourId)
                DECLARE_COLOUR_ID(TreeView::oddItemsColourId)
                DECLARE_COLOUR_ID(TreeView::evenItemsColourId)
                DECLARE_COLOUR_ID(Label::textColourId)
                DECLARE_COLOUR_ID(ComboBox::backgroundColourId)
                DECLARE_COLOUR_ID(TextEditor::backgroundColourId)
                DECLARE_COLOUR_ID(TextEditor::outlineColourId)
                DECLARE_COLOUR_ID(TextButton::buttonColourId)
                DECLARE_COLOUR_ID(TextButton::buttonOnColourId)

                DECLARE_COLOUR_ID(PopupMenu::backgroundColourId)
                DECLARE_COLOUR_ID(PopupMenu::textColourId)
                DECLARE_COLOUR_ID(PopupMenu::headerTextColourId)
                DECLARE_COLOUR_ID(PopupMenu::highlightedBackgroundColourId)
                DECLARE_COLOUR_ID(PopupMenu::highlightedTextColourId)

                DECLARE_COLOUR_ID(ListBox::backgroundColourId)
                DECLARE_COLOUR_ID(ListBox::outlineColourId)
                DECLARE_COLOUR_ID(ListBox::textColourId)

                DECLARE_COLOUR_ID(ResizableWindow::backgroundColourId)
                
                DECLARE_COLOUR_ID(Slider::thumbColourId)
                DECLARE_COLOUR_ID(Slider::trackColourId)
            };
        } // namespace


        std::optional<JucyTheme> ThemeManager::loadThemeFromFile(const std::filesystem::path &path)
        {
            try
            {
                toml::table tbl = toml::parse_file(path.string());

                JucyTheme theme;
                theme.name = tbl["name"].value_or("Unnamed Theme");
                spdlog::info("Loading theme: {} -----------------------------", theme.name);

                if (auto *colors = tbl["colors"].as_table())
                {
                    for (const auto &[key, value] : *colors)
                    {
                        const std::string nameOfColour{key.str()};

                        // Find the integer ColourId for this string key
                        const auto it = colourNameMap.find(nameOfColour);
                        if (it != colourNameMap.end())
                        {
                            const int idOfColour = it->second;
                            if (auto colorStr = value.value<std::string>())
                            {
                                // Parse the color string (e.g., "#RRGGBB")
                                const auto decodedColour = juce::Colour::fromString(*colorStr);
                                theme.colours[idOfColour] = decodedColour;
                                spdlog::info("Decoded color string '{}' with id {} to '#{}'", *colorStr, idOfColour, decodedColour.toString().toStdString());
                            }
                        }
                    }
                }
                return theme;
            }
            catch (const toml::parse_error &e)
            {
                spdlog::error("Failed to parse theme file '{}':\n{}", path.string(), e.what());
                return std::nullopt;
            }
        }

        void ThemeManager::initialize(const std::filesystem::path &themesFolderPath, const std::string& currentThemeName)
        {
            m_availableThemes.clear();
            for (const auto &entry : std::filesystem::directory_iterator(themesFolderPath))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".toml")
                {
                    if (auto theme = loadThemeFromFile(entry.path()))
                    {
                        m_availableThemes.push_back(*theme);
                    }
                }
            }
            m_currentThemeIndex = getThemeIndexByName(currentThemeName);
        }

        std::string ThemeManager::applyTheme(juce::LookAndFeel_V4& lookAndFeel, size_t themeIndex, juce::Component* pComponent)
        {
            // TODO: rework logging here
            if (themeIndex < m_availableThemes.size())
            {
                const auto& theme = m_availableThemes[themeIndex];
                m_currentThemeIndex = themeIndex; // Update current theme index
                //spdlog::info("Applying theme: {}", theme.name);

                for (const auto &[nameOfColour, intColourId] : colourNameMap)
                {
                    const auto it = theme.colours.find(intColourId);
                    if (it != theme.colours.end())
                    {
                        const auto colorToSet = it->second;
                        lookAndFeel.setColour(intColourId, colorToSet);
                        //spdlog::info("Set colour '{}' to '#{}'", nameOfColour, colorToSet.toString().toStdString());
                    }
                }
                if(pComponent)
                {
                    pComponent->setLookAndFeel(&lookAndFeel);
                }
                return theme.name;
            }
            return getNameOfTheme(0);
        }

        ThemeManager theThemeManager;
    } // namespace ui
} // namespace jucyaudio