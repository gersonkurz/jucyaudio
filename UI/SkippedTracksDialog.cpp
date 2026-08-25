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

#include <UI/SkippedTracksDialog.h>

#include <UI/CustomColourIds.h>
#include <Utils/UiUtils.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        SkippedTracksDialog::SkippedTracksDialog(const juce::String &summary, const std::vector<Entry> &entries)
        {
            m_summaryLabel.setText(summary, juce::dontSendNotification);
            m_summaryLabel.setJustificationType(juce::Justification::topLeft);
            addAndMakeVisible(m_summaryLabel);

            juce::String text;
            for (const auto &entry : entries)
            {
                if (text.isNotEmpty())
                {
                    text << juce::newLine;
                }

                // jucePathFromFs, not path.string(): the narrow form of a Windows path is not UTF-8, so
                // non-ASCII paths would display and copy wrongly.
                text << (entry.removedFromMix ? "[removed] " : "[kept]    ") << juce::String::fromUTF8(entry.name.c_str()) << juce::newLine
                     << "    " << jucePathFromFs(entry.path) << juce::newLine
                     << "    (" << juce::String::fromUTF8(entry.reason.c_str()) << ")" << juce::newLine;
            }

            m_listEditor.setMultiLine(true, false); // no soft wrap: long paths get a horizontal scrollbar
            m_listEditor.setReadOnly(true);
            m_listEditor.setScrollbarsShown(true);
            m_listEditor.setCaretVisible(false);
            m_listEditor.setPopupMenuEnabled(true); // so the list can be copied out
            m_listEditor.setFont(juce::Font{juce::FontOptions{juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain}});
            m_listEditor.setText(text, false);
            addAndMakeVisible(m_listEditor);

            m_closeButton.onClick = [this]()
            {
                if (auto *window = findParentComponentOfClass<juce::DialogWindow>())
                {
                    window->closeButtonPressed();
                }
            };
            addAndMakeVisible(m_closeButton);

            setSize(760, 420);
        }

        void SkippedTracksDialog::resized()
        {
            auto bounds = getLocalBounds().reduced(kMargin);

            m_summaryLabel.setBounds(bounds.removeFromTop(kSummaryHeight));
            bounds.removeFromTop(kMargin / 2);

            auto buttonRow = bounds.removeFromBottom(kButtonHeight);
            m_closeButton.setBounds(buttonRow.removeFromRight(kButtonWidth));
            bounds.removeFromBottom(kMargin / 2);

            m_listEditor.setBounds(bounds);
        }

        void SkippedTracksDialog::show(const juce::String &title,
            const juce::String &summary,
            const std::vector<Entry> &entries,
            juce::Component *parent)
        {
            if (entries.empty())
            {
                return;
            }

            juce::DialogWindow::LaunchOptions options;
            options.dialogTitle = title;
            options.content.setOwned(new SkippedTracksDialog{summary, entries});
            options.componentToCentreAround = parent;
            options.escapeKeyTriggersCloseButton = true;
            options.useNativeTitleBar = true;
            options.resizable = true;

            options.launchAsync();
        }

    } // namespace ui
} // namespace jucyaudio
