#pragma once

#include <JuceHeader.h>
#include <filesystem>
#include <Database/Includes/Constants.h>

namespace jucyaudio
{
    namespace ui
    {
        juce::String dataActionToString(database::DataAction action);
        juce::String getSafeDisplayText(const juce::String &text);
        juce::String formatWithThousandsSeparator(int number);
        std::filesystem::path jucePathToFs(const juce::String &jucePath);
        juce::String jucePathFromFs(const std::filesystem::path &path);
    } // namespace ui
} // namespace jucyaudio