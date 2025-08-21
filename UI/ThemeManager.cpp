#include <UI/CustomColourIds.h>
#include <UI/ThemeManager.h>
#include <spdlog/spdlog.h>
#include <toml++/toml.h> // Include the parser implementation here

namespace jucyaudio
{
    namespace ui
    {
        namespace
        {
            std::unordered_map<std::string, std::unordered_set<int>> semanticColourMap{
                {
                    "accent",
                    {
                        juce::ComboBox::backgroundColourId,
                        juce::PopupMenu::highlightedBackgroundColourId,
                        juce::TabbedButtonBar::frontOutlineColourId,
                        juce::Slider::thumbColourId,
                        juce::TextButton::buttonOnColourId,
                        juce::TreeView::dragAndDropIndicatorColourId,
                        juce::TreeView::selectedItemBackgroundColourId,
                        juce::ProgressBar::foregroundColourId,
                        juce::ScrollBar::thumbColourId
                    }
                },
                {
                    "mainBackground",
                    {
                        juce::ResizableWindow::backgroundColourId,
                        juce::TreeView::backgroundColourId,
                        juce::ListBox::backgroundColourId,
                        juce::TabbedComponent::backgroundColourId,
                        juce::TextEditor::backgroundColourId,
                        juce::PopupMenu::backgroundColourId,
                        juce::ProgressBar::backgroundColourId,
                        juce::Slider::textBoxBackgroundColourId,
                        juce::TreeView::evenItemsColourId,
                        juce::TextButton::buttonColourId,
                        juce::PopupMenu::highlightedTextColourId,
                    }
                },
                {
                    "alternateBackground", 
                    {
                        juce::TreeView::oddItemsColourId
                    }
                },
                {
                    "mainForeground",
                    {
                        juce::Label::textColourId,
                        juce::TextEditor::textColourId,
                        juce::ToggleButton::textColourId,
                        juce::PopupMenu::textColourId,
                        juce::PopupMenu::headerTextColourId,
                        juce::ListBox::textColourId,
                        juce::TabbedButtonBar::tabTextColourId,
                        juce::TabbedButtonBar::frontTextColourId,
                        juce::Slider::textBoxTextColourId,
                        jucyaudio::ui::folderOnlineTextColourId,
                        juce::TextButton::textColourOnId,
                    }
                },
                {
                    "disabledForeground", 
                    {
                        jucyaudio::ui::folderOfflineTextColourId, juce::TextButton::textColourOffId
                    }   
                },
            };

            const std::unordered_map<std::string, int> colourNameMap{
                {"TreeView::linesColourId", juce::TreeView::linesColourId},
                
                {"Label::textWhenEditingColourId", juce::Label::textWhenEditingColourId},
                {"Label::backgroundWhenEditingColourId", juce::Label::backgroundWhenEditingColourId},
                {"TextEditor::outlineColourId", juce::TextEditor::outlineColourId},

                {"ListBox::outlineColourId", juce::ListBox::outlineColourId},
                {"TabbedComponent::outlineColourId", juce::TabbedComponent::outlineColourId},
                {"TabbedButtonBar::tabOutlineColourId", juce::TabbedButtonBar::tabOutlineColourId},
                {"Slider::trackColourId", juce::Slider::trackColourId},                
                {"Slider::textBoxOutlineColourId", juce::Slider::textBoxOutlineColourId},
                {"ScrollBar::trackColourId", juce::ScrollBar::trackColourId},
                {"waveformColourId", jucyaudio::ui::waveformColourId},                
                {"accent", jucyaudio::ui::accentColourId},
                {"mainBackground", jucyaudio::ui::mainBackgroundColourId},
                {"alternateBackground", jucyaudio::ui::alternateBackgroundColourId},
                {"mainForeground", jucyaudio::ui::mainForegroundColourId},
                {"disabledForeground", jucyaudio::ui::disabledForegroundColourId},
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

                        const auto semanticKey{semanticColourMap.find(nameOfColour)};
                        if (semanticKey != semanticColourMap.end())
                        {
                            if (auto colorStr = value.value<std::string>())
                            {
                                // Parse the color string (e.g., "#RRGGBB")
                                const auto decodedColour{juce::Colour::fromString(*colorStr)};
                                spdlog::info("Decoded semantic {} to '#{}'", *colorStr, decodedColour.toString().toStdString());
                                for (const auto idOfColour : semanticKey->second)
                                {
                                    theme.colours[idOfColour] = decodedColour;
                                }
                            }
                        }

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

        void ThemeManager::initialize(const std::filesystem::path &themesFolderPath, const std::string &currentThemeName)
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

        std::string ThemeManager::applyTheme(juce::LookAndFeel_V4 &lookAndFeel, size_t themeIndex, juce::Component *pComponent)
        {
            // TODO: rework logging here
            if (themeIndex < m_availableThemes.size())
            {
                const auto &theme = m_availableThemes[themeIndex];
                m_currentThemeIndex = themeIndex; // Update current theme index
                // spdlog::info("Applying theme: {}", theme.name);

                // also apply semantic colours
                for (const auto &[nameOfColour, setOfColourIds] : semanticColourMap)
                {
                    for (const auto intColourId : setOfColourIds)
                    {
                        const auto it = theme.colours.find(intColourId);
                        if (it != theme.colours.end())
                        {
                            const auto colorToSet = it->second;
                            lookAndFeel.setColour(intColourId, colorToSet);
                            // spdlog::info("Set colour '{}' to '#{}'", nameOfColour, colorToSet.toString().toStdString());
                        }
                    }
                }

                for (const auto &[nameOfColour, intColourId] : colourNameMap)
                {
                    const auto it = theme.colours.find(intColourId);
                    if (it != theme.colours.end())
                    {
                        const auto colorToSet = it->second;
                        lookAndFeel.setColour(intColourId, colorToSet);
                        // spdlog::info("Set colour '{}' to '#{}'", nameOfColour, colorToSet.toString().toStdString());
                    }
                }
                if (pComponent)
                {
                    pComponent->setLookAndFeel(&lookAndFeel);
                    // Force all child components to update their colors
                    pComponent->sendLookAndFeelChange();
                }
                return theme.name;
            }
            return getNameOfTheme(0);
        }

        ThemeManager theThemeManager;
    } // namespace ui
} // namespace jucyaudio