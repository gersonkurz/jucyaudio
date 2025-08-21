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
                {"accentColourId",
                    {juce::ComboBox::backgroundColourId,
                        juce::PopupMenu::highlightedBackgroundColourId,
                        juce::TabbedButtonBar::frontOutlineColourId,
                        juce::Slider::thumbColourId,
                        juce::TextButton::buttonOnColourId,
                        juce::TreeView::dragAndDropIndicatorColourId,
                        juce::TreeView::selectedItemBackgroundColourId,
                        juce::ProgressBar::foregroundColourId,
                        juce::ScrollBar::thumbColourId}} // namespace ui
                ,
            };

            const std::unordered_map<std::string, int> colourNameMap{{"TreeView::backgroundColourId", juce::TreeView::backgroundColourId},
                {"TreeView::linesColourId", juce::TreeView::linesColourId},
                {"TreeView::oddItemsColourId", juce::TreeView::oddItemsColourId},
                {"TreeView::evenItemsColourId", juce::TreeView::evenItemsColourId},
                {"Label::textColourId", juce::Label::textColourId},
                {"Label::textWhenEditingColourId", juce::Label::textWhenEditingColourId},
                {"Label::backgroundWhenEditingColourId", juce::Label::backgroundWhenEditingColourId},
                {"TextEditor::backgroundColourId", juce::TextEditor::backgroundColourId},
                {"TextEditor::textColourId", juce::TextEditor::textColourId},
                {"TextEditor::outlineColourId", juce::TextEditor::outlineColourId},
                {"ToggleButton::textColourId", juce::ToggleButton::textColourId},
                {"TextButton::buttonColourId", juce::TextButton::buttonColourId},
                {"TextButton::textColourOffId", juce::TextButton::textColourOffId},
                {"TextButton::textColourOnId", juce::TextButton::textColourOnId},
                {"PopupMenu::backgroundColourId", juce::PopupMenu::backgroundColourId},
                {"PopupMenu::textColourId", juce::PopupMenu::textColourId},
                {"PopupMenu::headerTextColourId", juce::PopupMenu::headerTextColourId},
                {"PopupMenu::highlightedTextColourId", juce::PopupMenu::highlightedTextColourId},
                {"ListBox::backgroundColourId", juce::ListBox::backgroundColourId},
                {"ListBox::outlineColourId", juce::ListBox::outlineColourId},
                {"ListBox::textColourId", juce::ListBox::textColourId},
                {"ResizableWindow::backgroundColourId", juce::ResizableWindow::backgroundColourId},
                {"TabbedComponent::backgroundColourId", juce::TabbedComponent::backgroundColourId},
                {"TabbedComponent::outlineColourId", juce::TabbedComponent::outlineColourId},
                {"TabbedButtonBar::tabOutlineColourId", juce::TabbedButtonBar::tabOutlineColourId},
                {"TabbedButtonBar::tabTextColourId", juce::TabbedButtonBar::tabTextColourId},
                {"TabbedButtonBar::frontTextColourId", juce::TabbedButtonBar::frontTextColourId},
                {"Slider::trackColourId", juce::Slider::trackColourId},
                {"Slider::textBoxTextColourId", juce::Slider::textBoxTextColourId},
                {"Slider::textBoxBackgroundColourId", juce::Slider::textBoxBackgroundColourId},
                {"Slider::textBoxOutlineColourId", juce::Slider::textBoxOutlineColourId},
                {"ScrollBar::trackColourId", juce::ScrollBar::trackColourId},
                {"ProgressBar::backgroundColourId", juce::ProgressBar::backgroundColourId},
                {"waveformColourId", jucyaudio::ui::waveformColourId},
                {"folderOnlineTextColourId", jucyaudio::ui::folderOnlineTextColourId},
                {"folderOfflineTextColourId", jucyaudio::ui::folderOfflineTextColourId},
                {"accentColourId", jucyaudio::ui::accentColourId}};
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