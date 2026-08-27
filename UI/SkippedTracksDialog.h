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
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>
#include <vector>

namespace jucyaudio
{
    namespace ui
    {
        /**
         * @brief Lists tracks that could not be used, one per line, in a resizable window.
         *
         * Replaces squeezing a list of filenames into a single fixed-width status line, which is
         * unreadable past two or three entries. The list is a read-only editor rather than a label so
         * the paths can be scrolled and copied out to go and fix the files.
         *
         * Two modes: show() reports what already happened, showConfirm() asks before it happens.
         */
        class SkippedTracksDialog final : public juce::Component
        {
        public:
            /// @brief What was done with a listed track, where there is anything to report.
            enum class Disposition
            {
                NotApplicable, ///< Nothing has happened yet - the list is being shown to ask, not to report.
                Kept,          ///< Left where it was.
                Removed        ///< Taken out.
            };

            /// @brief One listed track: what it was, where it lives, and why it is here.
            struct Entry final
            {
                std::string name;
                std::filesystem::path path;
                std::string reason;
                Disposition disposition{Disposition::NotApplicable};
            };

            /// @brief Called once with the user's answer. See showConfirm().
            using OnChoice = std::function<void(bool confirmed)>;

            /// @brief Report the list. Non-blocking; the window owns itself.
            /// @param title Window title.
            /// @param summary One-line explanation shown above the list.
            /// @param entries The tracks to list. Showing an empty list is a no-op.
            /// @param parent Component to centre on; may be null.
            static void show(const juce::String &title,
                const juce::String &summary,
                const std::vector<Entry> &entries,
                juce::Component *parent);

            /// @brief Show the list and ask the user to confirm or cancel. Non-blocking.
            /// @param title Window title.
            /// @param summary One-line explanation shown above the list.
            /// @param entries The tracks to list. An empty list confirms immediately - there is nothing to object to.
            /// @param confirmButtonText Label for the confirming button.
            /// @param cancelButtonText Label for the cancelling button.
            /// @param parent Component to centre on; may be null.
            /// @param onChoice Runs exactly once on the message thread: true if the user confirmed, false for
            ///        cancel, Escape, or closing the window. Never runs twice, and never fails to run.
            static void showConfirm(const juce::String &title,
                const juce::String &summary,
                const std::vector<Entry> &entries,
                const juce::String &confirmButtonText,
                const juce::String &cancelButtonText,
                juce::Component *parent,
                OnChoice onChoice);

            ~SkippedTracksDialog() override;

            void resized() override;

        private:
            /// @param confirmButtonText Empty for report-only mode: no confirming button is shown.
            SkippedTracksDialog(const juce::String &summary,
                const std::vector<Entry> &entries,
                const juce::String &confirmButtonText,
                const juce::String &cancelButtonText,
                OnChoice onChoice);

            static void launch(const juce::String &title, SkippedTracksDialog *content, juce::Component *parent);

            void reportChoice(bool confirmed);
            void closeEnclosingWindow();

            static constexpr int kMargin = 12;
            static constexpr int kSummaryHeight = 40;
            static constexpr int kButtonHeight = 26;
            static constexpr int kButtonWidth = 90;
            static constexpr int kButtonGap = 8;

            juce::Label m_summaryLabel;
            juce::TextEditor m_listEditor;
            juce::TextButton m_confirmButton;
            juce::TextButton m_cancelButton;

            OnChoice m_onChoice;
            bool m_choiceReported{false};

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkippedTracksDialog)
        };

    } // namespace ui
} // namespace jucyaudio
