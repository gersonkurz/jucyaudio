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
 */

#pragma once

#include <Database/Includes/Constants.h>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace ui
    {
        /**
         * @brief Reports tracks that had to be skipped, one per line, in a resizable window.
         *
         * Replaces squeezing a list of filenames into a single fixed-width status line, which is
         * unreadable past two or three entries. The list is a read-only editor rather than a label so
         * the paths can be scrolled and copied out to go and fix the files.
         */
        class SkippedTracksDialog final : public juce::Component
        {
        public:
            /// @brief One skipped track: what it was, where it lives, and why it was skipped.
            struct Entry final
            {
                std::string name;
                std::filesystem::path path;
                std::string reason;
                bool removedFromMix{false};
            };

            /// @brief Show the dialog. Non-blocking; the window owns itself.
            /// @param title Window title.
            /// @param summary One-line explanation shown above the list.
            /// @param entries The skipped tracks. Showing an empty list is a no-op.
            /// @param parent Component to centre on; may be null.
            static void show(const juce::String &title,
                const juce::String &summary,
                const std::vector<Entry> &entries,
                juce::Component *parent);

            void resized() override;

        private:
            SkippedTracksDialog(const juce::String &summary, const std::vector<Entry> &entries);

            static constexpr int kMargin = 12;
            static constexpr int kSummaryHeight = 40;
            static constexpr int kButtonHeight = 26;
            static constexpr int kButtonWidth = 90;

            juce::Label m_summaryLabel;
            juce::TextEditor m_listEditor;
            juce::TextButton m_closeButton{"Close"};

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkippedTracksDialog)
        };

    } // namespace ui
} // namespace jucyaudio
