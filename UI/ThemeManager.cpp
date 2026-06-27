#include <UI/CustomColourIds.h>
#include <UI/ThemeManager.h>
#include <spdlog/spdlog.h>
#include <toml++/toml.h> // Include the parser implementation here
#include <UI/CheckboxLookAndFeel.h>
#include <fkYAML/node.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>

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
                        juce::ToggleButton::tickColourId,
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
                        jucyaudio::ui::folderOfflineTextColourId,
                        juce::TextButton::textColourOffId,
                        juce::ToggleButton::tickDisabledColourId,
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

            // Parse a base16 hex colour ("#RRGGBB" or "RRGGBB") into an opaque juce::Colour.
            juce::Colour parseHex6(std::string s)
            {
                if (!s.empty() && s.front() == '#')
                {
                    s.erase(0, 1);
                }
                const auto v{static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 16))};
                return juce::Colour{0xff000000u | (v & 0x00ffffffu)};
            }

            // The crux of base16 theming: map the 16 palette slots (base00..base0F) onto
            // jucyaudio's semantic colour roles. Single source of truth, so every theme is
            // just 16 colours. base00 = bg, base05 = fg, base03 = muted, base0D = accent.
            std::unordered_map<std::string, juce::Colour> resolveBase16(const std::array<juce::Colour, 16> &b)
            {
                return {
                    {"mainBackground", b[0]},                       // base00 - default background
                    {"alternateBackground", b[1]},                  // base01 - lighter background (rows)
                    {"mainForeground", b[5]},                       // base05 - default foreground
                    {"disabledForeground", b[3]},                   // base03 - comments / muted
                    {"accent", b[13]},                              // base0D - blue accent (per theme)
                    {"waveformColourId", b[13]},                    // base0D - accent-coloured waveform
                    {"TreeView::linesColourId", b[3]},              // base03
                    {"Label::textWhenEditingColourId", b[5]},       // base05
                    {"Label::backgroundWhenEditingColourId", b[0]}, // base00
                    {"TextEditor::outlineColourId", b[2]},          // base02 - selection/border
                    {"ListBox::outlineColourId", b[2]},
                    {"TabbedComponent::outlineColourId", b[2]},
                    {"TabbedButtonBar::tabOutlineColourId", b[2]},
                    {"Slider::trackColourId", b[2]},
                    {"Slider::textBoxOutlineColourId", b[2]},
                    {"ScrollBar::trackColourId", b[1]},             // base01
                };
            }

            // The 16 base16 palette slot names, in order (base00..base0F).
            constexpr std::array<const char *, 16> kBase16Slots{"base00",
                "base01",
                "base02",
                "base03",
                "base04",
                "base05",
                "base06",
                "base07",
                "base08",
                "base09",
                "base0A",
                "base0B",
                "base0C",
                "base0D",
                "base0E",
                "base0F"};

            // Load a base16 scheme from a .yaml/.yml file via fkYAML — the standard distribution
            // format, so public base16 schemes can be dropped into Themes/ unmodified. Fills the
            // theme's name/palette/isDark and returns the resolved semantic colours, or nullopt.
            std::optional<std::unordered_map<std::string, juce::Colour>> loadBase16Yaml(const std::filesystem::path &path, JucyTheme &theme)
            {
                std::ifstream in{path, std::ios::binary};
                if (!in)
                {
                    spdlog::error("Cannot open theme file '{}'", path.string());
                    return std::nullopt;
                }
                const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};

                fkyaml::node root;
                try
                {
                    root = fkyaml::node::deserialize(text);
                }
                catch (const std::exception &e)
                {
                    spdlog::error("YAML parse error in '{}': {}", path.string(), e.what());
                    return std::nullopt;
                }

                if (!root.is_mapping())
                {
                    spdlog::error("Theme '{}': top-level YAML is not a mapping", path.string());
                    return std::nullopt;
                }

                // Colours live either under a `palette:` map or at the top level.
                const fkyaml::node *colorMap = &root;
                if (root.contains("palette") && root["palette"].is_mapping())
                {
                    colorMap = &root["palette"];
                }

                theme.name = (root.contains("name") && root["name"].is_string()) ? root["name"].get_value<std::string>() : path.stem().string();

                std::array<juce::Colour, 16> base;
                for (size_t i = 0; i < base.size(); ++i)
                {
                    if (!colorMap->contains(kBase16Slots[i]) || !(*colorMap)[kBase16Slots[i]].is_string())
                    {
                        spdlog::error("Theme '{}' is missing palette slot '{}'", theme.name, kBase16Slots[i]);
                        return std::nullopt;
                    }
                    base[i] = parseHex6((*colorMap)[kBase16Slots[i]].get_value<std::string>());
                }

                theme.palette = base;
                if (root.contains("variant") && root["variant"].is_string())
                {
                    auto variant = root["variant"].get_value<std::string>();
                    std::transform(variant.begin(), variant.end(), variant.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    theme.isDark = (variant != "light");
                }
                else
                {
                    theme.isDark = base[0].getPerceivedBrightness() < 0.5f;
                }
                return resolveBase16(base);
            }
        } // namespace

        std::optional<JucyTheme> ThemeManager::loadThemeFromFile(const std::filesystem::path &path)
        {
            JucyTheme theme;
            // Resolved semantic-name -> colour map, populated from whichever format this file is.
            std::unordered_map<std::string, juce::Colour> values;

            const auto ext = path.extension().string();
            if (ext == ".yaml" || ext == ".yml")
            {
                // Standard base16 distribution format, parsed via fkYAML.
                auto resolved = loadBase16Yaml(path, theme);
                if (!resolved)
                {
                    return std::nullopt;
                }
                values = std::move(*resolved);
            }
            else
            {
                // .toml: a base16 [palette] table (current format) or a legacy [colors] table.
                try
                {
                    toml::table tbl = toml::parse_file(path.string());
                    theme.name = tbl["name"].value_or("Unnamed Theme");

                    if (auto *palette = tbl["palette"].as_table())
                    {
                        std::array<juce::Colour, 16> base;
                        for (size_t i = 0; i < base.size(); ++i)
                        {
                            const auto hex{(*palette)[kBase16Slots[i]].value<std::string>()};
                            if (!hex)
                            {
                                spdlog::error("Theme '{}' is missing palette slot '{}'", theme.name, kBase16Slots[i]);
                                return std::nullopt;
                            }
                            base[i] = parseHex6(*hex);
                        }
                        values = resolveBase16(base);

                        // Keep the raw palette for swatch previews; classify light/dark from the
                        // declared variant, falling back to base00 (background) brightness.
                        theme.palette = base;
                        if (const auto variant{tbl["variant"].value<std::string>()})
                        {
                            theme.isDark = (*variant != "light");
                        }
                        else
                        {
                            theme.isDark = base[0].getPerceivedBrightness() < 0.5f;
                        }
                    }
                    else if (auto *colors = tbl["colors"].as_table())
                    {
                        for (const auto &[key, value] : *colors)
                        {
                            if (auto colorStr = value.value<std::string>())
                            {
                                values[std::string{key.str()}] = juce::Colour::fromString(*colorStr);
                            }
                        }
                        if (const auto bg{values.find("mainBackground")}; bg != values.end())
                        {
                            theme.isDark = bg->second.getPerceivedBrightness() < 0.5f;
                        }
                    }
                }
                catch (const toml::parse_error &e)
                {
                    spdlog::error("Failed to parse theme file '{}':\n{}", path.string(), e.what());
                    return std::nullopt;
                }
            }

            spdlog::info("Loading theme: {} -----------------------------", theme.name);

            // Fan each semantic name out to the JUCE ColourIds it drives.
                for (const auto &[nameOfColour, colour] : values)
                {
                    if (const auto semanticKey{semanticColourMap.find(nameOfColour)}; semanticKey != semanticColourMap.end())
                    {
                        for (const auto idOfColour : semanticKey->second)
                        {
                            theme.colours[idOfColour] = colour;
                        }
                    }
                    if (const auto it{colourNameMap.find(nameOfColour)}; it != colourNameMap.end())
                    {
                        theme.colours[it->second] = colour;
                    }
                }
            return theme;
        }

        void ThemeManager::initialize(const std::filesystem::path &themesFolderPath, const std::string &currentThemeName)
        {
            m_availableThemes.clear();
            for (const auto &entry : std::filesystem::directory_iterator(themesFolderPath))
            {
                const auto ext = entry.path().extension().string();
                if (entry.is_regular_file() && (ext == ".toml" || ext == ".yaml" || ext == ".yml"))
                {
                    if (auto theme = loadThemeFromFile(entry.path()))
                    {
                        m_availableThemes.push_back(*theme);
                    }
                }
            }
            m_currentThemeIndex = getThemeIndexByName(currentThemeName);

            // If the saved theme name no longer exists (e.g. a legacy "Jucy Dark" after the
            // base16 migration), getThemeIndexByName() silently returns 0 — which would land
            // an upgrading user on an arbitrary (alphabetically first) theme. Prefer the brand
            // theme instead so the default stays a dark, orange-accented look.
            bool found = false;
            for (const auto &t : m_availableThemes)
            {
                if (t.name == currentThemeName)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                m_currentThemeIndex = getThemeIndexByName("JucyAudio Orange");
            }
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
                    // Also set as default so dialogs get it too
                    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
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