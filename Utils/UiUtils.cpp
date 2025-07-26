#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <Utils/UiUtils.h>
#include <filesystem>

namespace jucyaudio
{
    namespace ui
    {
        using namespace database;

        // Helper to convert DataAction to a displayable string (simple version)
        // In a real app, this might be more sophisticated, perhaps with localization.
        juce::String dataActionToString(DataAction action, const INavigationNode* node)
        {
            switch (action)
            {
            case DataAction::None:
                return "None";
            case DataAction::Play:
                return "Play";
            case DataAction::CreateWorkingSet:
                return "Create Working Set";
            case DataAction::CreateMix:
                return "Create Mix";
            case DataAction::Delete:
                return std::format("Delete {}", node->m_refTypeNameForSingleObject);
            case DataAction::ExportMix:
                return "Export Mix";
            case DataAction::ShowDetails:
                return "Details";
            case DataAction::EditWorkingSetMetadata:
                return "Edit Working-Set Metadata";
            case DataAction::RemoveTracks:
                return "Remove Tracks";
            case DataAction::RunBpmAnalysis:
                return "Run BPM Analysis";
            case DataAction::ShowMixEditor:
                return "Show Mix Editor";
            case DataAction::ShowTrackEditor:
                return "Show Track Editor";
            case DataAction::Separator:
                return "------";
            default:
                return "dataActionToString()?";
            }
        }

        DataAction showDataActionPopup(const DataActions &availableActions, const INavigationNode *node, MainViewType mainViewType)
        {
            if (availableActions.empty())
                return DataAction::None;

            juce::PopupMenu menu;
            for (size_t i = 0; i < availableActions.size(); ++i)
            {
                bool isTicked = false;
                const auto action{availableActions[i]};
                if (action == DataAction::Separator)
                {
                    menu.addSeparator();
                }
                else if (action == DataAction::None)
                {
                    // Skip None action, it doesn't make sense to show it in the menu
                    continue;
                }
                else
                {
                    if (action == DataAction::ShowMixEditor && mainViewType == MainViewType::MixEditor)
                    {
                        isTicked = true; // ShowMixEditor is always ticked in MixEditor view
                    }
                    else if (action == DataAction::ShowTrackEditor && mainViewType == MainViewType::DataView)
                    {
                        isTicked = true; // ShowTrackEditor is always ticked in DataView
                    }
                    menu.addItem(static_cast<int>(i + 1), dataActionToString(action, node), true, isTicked);
                }
            }

            const int result = menu.show();
            if (result > 0 && result <= static_cast<int>(availableActions.size()))
            {
                return availableActions[result - 1];
            }
            return DataAction::None;
        }

        juce::String getSafeDisplayText(const juce::String &text)
        {
            juce::String result;

            for (int i = 0; i < text.length(); ++i)
            {
                juce::juce_wchar ch = text[i];

                // Keep "safe" characters: ASCII + basic Latin extended
                if (ch <= 127 || (ch >= 160 && ch <= 255))
                {
                    result += ch;
                }
                else
                {
                    result += "?"; // Replace with question mark
                }
            }

            return result;
        }
        std::filesystem::path jucePathToFs(const juce::String &jucePath)
        {
#if defined(_WIN32)
            return std::filesystem::path{reinterpret_cast<const char8_t *>(jucePath.toRawUTF8())};
#else
            return std::filesystem::path{jucePath.toStdString()};
#endif
        }

        juce::String jucePathFromFs(const std::filesystem::path &path)
        {
#if defined(_WIN32)
            const auto &u8str = path.u8string();
            return juce::CharPointer_UTF8(reinterpret_cast<const char *>(u8str.c_str()));
#else
            return juce::String::fromUTF8(path.string().c_str());
#endif
        }

       
    }
}