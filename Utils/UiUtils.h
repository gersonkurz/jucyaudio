/*
 * This file is part of jucyaudio.
 * Copyright (C) 2025 Gerson Kurz <not@p-nand-q.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */                                                                                                                                                            \
#pragma once

#include <Database/Includes/Constants.h>
#include <Database/Includes/INavigationNode.h>
#include <filesystem>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * @file UiUtils.h
 * @brief User interface utility functions for the jucyaudio application
 *
 * This header provides utility functions for UI-related operations including
 * string formatting, data action conversions, path handling, and text processing
 * specifically designed for use with the JUCE framework.
 */

namespace jucyaudio
{
    namespace ui
    {
        using namespace database;

        enum class MainViewType
        {
            DataView,
            MixEditor
        };
        
        // @brief Shows a popup menu for available data actions
        // @param availableActions List of actions to display in the popup
        // @param node The current navigation node context for the actions
        // @param mainViewType The type of the main view (DataView or MixEditor)
        // @return The selected DataAction from the popup menu, or DataAction::None if cancelled
        DataAction showDataActionPopup(const DataActions &availableActions, const INavigationNode *node, MainViewType mainViewType);
        
        // @brief Converts a database action enum to a human-readable string
        // @param action The DataAction enum value to convert
        // @return JUCE string representation of the action suitable for display in UI
        // @note Returns "Unknown Action" for unrecognized action values
        // @see DataAction
        juce::String dataActionToString(DataAction action, const INavigationNode *node);

        // @brief Gets an icon drawable for a database action
        // @param action The DataAction enum value to get icon for
        // @param lookAndFeel Optional LookAndFeel to get accent color from
        // @return Unique pointer to a Drawable for the action icon
        // @note Returns nullptr if no icon is available
        // @see DataAction
        std::unique_ptr<juce::Drawable> dataActionToIcon(DataAction action, const juce::LookAndFeel* lookAndFeel = nullptr);

        // @brief Sanitizes text for safe display by filtering out problematic Unicode characters
        // @param text The input text to sanitize
        // @return JUCE string with non-ASCII characters (except basic Latin extended) replaced with "?"
        // @note Keeps ASCII characters (0-127) and basic Latin extended characters (160-255)
        // @note Useful for preventing display issues with unsupported Unicode characters
        juce::String getSafeDisplayText(const juce::String &text);

        // @brief Formats an integer with thousands separators for improved readability
        // @param number The integer to format
        // @return JUCE string with thousands separators (dots) inserted every 3 digits
        // @note Uses dots as thousands separators (e.g., 1.234.567)
        // @note Handles negative numbers correctly by preserving the minus sign
        // @example formatWithThousandsSeparator(1234567) returns "1.234.567"
        template <typename T> T formatStringTypeWithThousandsSeparator(int number)
        {
            const std::string str{std::to_string(std::abs(number))};
            std::string result;

            int digitCount = 0;
            for (int i = static_cast<int>(str.length()) - 1; i >= 0; --i)
            {
                if (digitCount > 0 && digitCount % 3 == 0)
                    result = '.' + result;

                result = str[i] + result;
                ++digitCount;
            }

            if (number < 0)
                result = '-' + result;

            return T{result};
        }

        inline juce::String formatJuceStringNumber(int number)
        {
            return formatStringTypeWithThousandsSeparator<juce::String>(number);
        }

        inline std::string formatStandardStringNumber(int number)
        {
            return formatStringTypeWithThousandsSeparator<std::string>(number);
        }

        // @brief Copies text to the system clipboard.
        // @param text The text to place on the clipboard (replaces any existing content).
        void copyTextToClipboard(const juce::String &text);

        // @brief Converts a JUCE string path to std::filesystem::path
        // @param jucePath The JUCE string containing a file system path
        // @return std::filesystem::path object with proper UTF-8 encoding
        // @note Handles platform-specific path encoding differences
        // @note On Windows, uses UTF-8 encoding; on other platforms uses standard string conversion
        // @see jucePathFromFs for the reverse conversion
        std::filesystem::path jucePathToFs(const juce::String &jucePath);

        // @brief Converts a std::filesystem::path to JUCE string
        // @param path The filesystem path to convert
        // @return JUCE string representation of the path with proper UTF-8 handling
        // @note Handles platform-specific path encoding differences
        // @note On Windows, uses UTF-8 encoding; on other platforms uses standard string conversion
        // @see jucePathToFs for the reverse conversion
        juce::String jucePathFromFs(const std::filesystem::path &path);

    } // namespace ui
} // namespace jucyaudio