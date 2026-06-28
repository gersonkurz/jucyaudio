#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <Utils/UiUtils.h>
#include <UI/CustomColourIds.h>
#include <filesystem>
#include <BinaryData.h>

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
                return node ? std::format("Delete {}", node->m_refTypeNameForSingleObject) : "Delete";
            case DataAction::ExportMix:
                return "Export Mix";
            case DataAction::ShowDetails:
                return "Details";
            case DataAction::EditWorkingSetMetadata:
                return "Edit Working-Set Metadata";
            case DataAction::EditMixMetadata:
                return "Edit Mix Metadata";
            case DataAction::RemoveTracks:
                return "Remove Tracks";
            case DataAction::RunBpmAnalysis:
                return "Run BPM Analysis";
            case DataAction::ShowMixEditor:
                return "Show Mix Editor";
            case DataAction::ShowTrackEditor:
                return "Show Track Editor";
            case DataAction::ShowInFolder:
                return "Show in Folder";
            case DataAction::Separator:
                return "------";
            case DataAction::RemoveDuplicates:
                return "Remove Duplicates";
            case DataAction::DeleteTracksFromLibrary:
                return "Delete Tracks...";
            case DataAction::Settings:
                return "Settings";
            case DataAction::ScanFolders:
                return "Scan Folders";
            case DataAction::ShowEqualizer:
                return "Equalizer";
            case DataAction::ShowReverb:
                return "Reverb";
            case DataAction::ShowMasterEffects:
                return "Master Effects";
            case DataAction::ToggleMasterEffectsBypass:
                return "Bypass Master FX";
            case DataAction::UnlockMixForEditing:
                return "Unlock for Editing";
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

        std::unique_ptr<juce::Drawable> dataActionToIcon(DataAction action, const juce::LookAndFeel* lookAndFeel)
        {
            if(!lookAndFeel)
                return nullptr;
            const auto originalIconColour = juce::Colour(0xFFF96E00);  // Standardized orange in all SVGs
            const bool isThemeLoaded = lookAndFeel->isColourSpecified(jucyaudio::ui::accentColourId);
            
            // Helper to load SVG from binary data and apply accent color
            auto loadSvg = [&](const char *data, size_t size) -> std::unique_ptr<juce::Drawable>
            {
                auto drawable = juce::Drawable::createFromImageData(data, size);
                if (drawable && lookAndFeel && isThemeLoaded)
                {
                    const auto accentColour = lookAndFeel->findColour(jucyaudio::ui::accentColourId);
                    if (accentColour != originalIconColour)
                        drawable->replaceColour(originalIconColour, accentColour);
                }
                return drawable;
            };

            // For now, use play_arrow.svg as a placeholder for all actions
            // This will be replaced with specific icons once they're available
            switch (action)
            {
            case DataAction::Play:
                return loadSvg(BinaryData::play_arrow_svg, BinaryData::play_arrow_svgSize);
            case DataAction::CreateWorkingSet:
                return loadSvg(BinaryData::create_working_set_svg, BinaryData::create_working_set_svgSize); // Placeholder
            case DataAction::CreateMix:
                return loadSvg(BinaryData::create_mix_svg, BinaryData::create_mix_svgSize); // Placeholder
            case DataAction::ShowDetails:
                return loadSvg(BinaryData::show_details_svg, BinaryData::show_details_svgSize); // Placeholder
            case DataAction::EditWorkingSetMetadata:
                return loadSvg(BinaryData::edit_workingset_metadata_svg, BinaryData::edit_workingset_metadata_svgSize); // Placeholder
            case DataAction::EditMixMetadata:
                return loadSvg(BinaryData::edit_mix_metadata_svg, BinaryData::edit_mix_metadata_svgSize); // Placeholder //-V1037
            case DataAction::RemoveTracks:
                return loadSvg(BinaryData::remove_tracks_svg, BinaryData::remove_tracks_svgSize); // Placeholder
            case DataAction::Delete:
                return loadSvg(BinaryData::delete_svg, BinaryData::delete_svgSize); // Placeholder //-V1037
            case DataAction::ExportMix:
                return loadSvg(BinaryData::export_mix_svg, BinaryData::export_mix_svgSize); // Placeholder
            case DataAction::RunBpmAnalysis:
                return loadSvg(BinaryData::run_bpm_analysis_svg, BinaryData::run_bpm_analysis_svgSize); // Placeholder
            case DataAction::ShowMixEditor:
                return loadSvg(BinaryData::show_mix_editor_svg, BinaryData::show_mix_editor_svgSize); // Placeholder
            case DataAction::ShowTrackEditor:
                return loadSvg(BinaryData::show_track_editor_svg, BinaryData::show_track_editor_svgSize); // Placeholder
            case DataAction::ShowInFolder:
                return loadSvg(BinaryData::show_in_folder_svg, BinaryData::show_in_folder_svgSize); // Placeholder
            case DataAction::RemoveDuplicates:
                return loadSvg(BinaryData::remove_duplicates_svg, BinaryData::remove_duplicates_svgSize); // Placeholder
            case DataAction::DeleteTracksFromLibrary:
                return loadSvg(BinaryData::delete_svg, BinaryData::delete_svgSize); // Reuse delete icon
            case DataAction::Settings:
                return loadSvg(BinaryData::settings_svg, BinaryData::settings_svgSize);
            case DataAction::ScanFolders:
                return loadSvg(BinaryData::scan_folders_svg, BinaryData::scan_folders_svgSize);
            case DataAction::ShowEqualizer:
                return loadSvg(BinaryData::equalizer_svg, BinaryData::equalizer_svgSize);
            case DataAction::ShowReverb:
                return loadSvg(BinaryData::reverb_svg, BinaryData::reverb_svgSize);
            case DataAction::ShowMasterEffects:
                return loadSvg(BinaryData::plugins_svg, BinaryData::plugins_svgSize);
            case DataAction::ToggleMasterEffectsBypass:
                return loadSvg(BinaryData::bypass_svg, BinaryData::bypass_svgSize);
            case DataAction::UnlockMixForEditing:
                return loadSvg(BinaryData::edit_mix_metadata_svg, BinaryData::edit_mix_metadata_svgSize); // Using edit icon as placeholder
            case DataAction::None:
            case DataAction::Separator:
            default:
                return nullptr;
            }
        }

        juce::String getSafeDisplayText(const juce::String &text)
        {
            // Pass through all printable Unicode so non-Latin names (CJK, emoji, the
            // mathematical-fraktur letters some releases use, etc.) render correctly — the
            // same text the list view shows. Only strip control characters, which can
            // corrupt a single-line layout. (Previously this replaced everything above
            // U+00FF with '?', turning any supplementary-plane name into "????????".)
            juce::String result;
            for (auto cp = text.getCharPointer(); !cp.isEmpty();)
            {
                const juce::juce_wchar ch = cp.getAndAdvance();
                if (ch >= 32 || ch == '\t')
                {
                    result += ch;
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
