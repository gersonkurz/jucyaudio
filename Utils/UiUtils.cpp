#include <JuceHeader.h>
#include <Utils/UiUtils.h>
#include <filesystem>

namespace jucyaudio
{
    namespace ui
    {

        // Helper to convert DataAction to a displayable string (simple version)
        // In a real app, this might be more sophisticated, perhaps with localization.
        juce::String dataActionToString(database::DataAction action)
        {
            switch (action)
            {
            case database::DataAction::None:
                return "None";
            case database::DataAction::Play:
                return "Play";
            case database::DataAction::CreateWorkingSet:
                return "Create Working Set";
            case database::DataAction::CreateMix:
                return "Create Mix";
            case database::DataAction::RemoveMix:
                return "Remove Mix";
            case database::DataAction::ShowDetails:
                return "Details";
            case database::DataAction::EditMetadata:
                return "Edit";
            case database::DataAction::Delete:
                return "Delete";
            default:
                return "Unknown Action";
            }
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
            return juce::String(juce::CharPointer_UTF8(reinterpret_cast<const char *>(u8str.c_str())));
#else
            return juce::String(juce::CharPointer_UTF8(path.string().c_str()));
#endif
        }

        juce::String formatWithThousandsSeparator(int number)
        {
            juce::String str(std::abs(number));
            juce::String result;

            int digitCount = 0;
            for (int i = str.length() - 1; i >= 0; --i)
            {
                if (digitCount > 0 && digitCount % 3 == 0)
                    result = "." + result;

                result = str[i] + result;
                digitCount++;
            }

            if (number < 0)
                result = "-" + result;

            return result;
        }

    }
}