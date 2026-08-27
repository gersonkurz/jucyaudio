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
        SkippedTracksDialog::SkippedTracksDialog(const juce::String &summary,
            const std::vector<Entry> &entries,
            const juce::String &confirmButtonText,
            const juce::String &cancelButtonText,
            OnChoice onChoice)
            : m_onChoice{std::move(onChoice)}
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

                switch (entry.disposition)
                {
                case Disposition::Removed:
                    text << "[removed] ";
                    break;
                case Disposition::Kept:
                    text << "[kept]    ";
                    break;
                case Disposition::NotApplicable:
                    break;
                }

                // jucePathFromFs, not path.string(): the narrow form of a Windows path is not UTF-8, so
                // non-ASCII paths would display and copy wrongly.
                text << juce::String::fromUTF8(entry.name.c_str()) << juce::newLine
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

            if (confirmButtonText.isNotEmpty())
            {
                m_confirmButton.setButtonText(confirmButtonText);
                m_confirmButton.onClick = [this]()
                {
                    reportChoice(true);
                    closeEnclosingWindow();
                };
                addAndMakeVisible(m_confirmButton);
            }

            m_cancelButton.setButtonText(cancelButtonText);
            m_cancelButton.onClick = [this]()
            {
                reportChoice(false);
                closeEnclosingWindow();
            };
            addAndMakeVisible(m_cancelButton);

            setSize(760, 420);
        }

        SkippedTracksDialog::~SkippedTracksDialog()
        {
            // Escape and the title-bar close button destroy the window without going through either
            // button, so the caller would otherwise wait forever for an answer that never comes.
            reportChoice(false);
        }

        void SkippedTracksDialog::reportChoice(bool confirmed)
        {
            if (m_choiceReported || !m_onChoice)
            {
                return;
            }
            m_choiceReported = true;

            // Deferred rather than called here: this also runs from the destructor, and the caller is
            // free to open another dialog in response, which must not happen while this one is being
            // torn down.
            juce::MessageManager::callAsync(
                [callback = std::move(m_onChoice), confirmed]()
                {
                    callback(confirmed);
                });
        }

        void SkippedTracksDialog::closeEnclosingWindow()
        {
            if (auto *window = findParentComponentOfClass<juce::DialogWindow>())
            {
                window->closeButtonPressed();
            }
        }

        void SkippedTracksDialog::resized()
        {
            auto bounds = getLocalBounds().reduced(kMargin);

            m_summaryLabel.setBounds(bounds.removeFromTop(kSummaryHeight));
            bounds.removeFromTop(kMargin / 2);

            auto buttonRow = bounds.removeFromBottom(kButtonHeight);
            m_cancelButton.setBounds(buttonRow.removeFromRight(juce::jmax(kButtonWidth, m_cancelButton.getBestWidthForHeight(kButtonHeight))));
            if (m_confirmButton.isVisible())
            {
                buttonRow.removeFromRight(kButtonGap);
                m_confirmButton.setBounds(buttonRow.removeFromRight(juce::jmax(kButtonWidth, m_confirmButton.getBestWidthForHeight(kButtonHeight))));
            }
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

            launch(title, new SkippedTracksDialog{summary, entries, {}, "Close", {}}, parent);
        }

        void SkippedTracksDialog::showConfirm(const juce::String &title,
            const juce::String &summary,
            const std::vector<Entry> &entries,
            const juce::String &confirmButtonText,
            const juce::String &cancelButtonText,
            juce::Component *parent,
            OnChoice onChoice)
        {
            if (entries.empty())
            {
                // Nothing to object to, so there is nothing to ask about.
                if (onChoice)
                {
                    onChoice(true);
                }
                return;
            }

            launch(title, new SkippedTracksDialog{summary, entries, confirmButtonText, cancelButtonText, std::move(onChoice)}, parent);
        }

        void SkippedTracksDialog::launch(const juce::String &title, SkippedTracksDialog *content, juce::Component *parent)
        {
            juce::DialogWindow::LaunchOptions options;
            options.dialogTitle = title;
            options.content.setOwned(content);
            options.componentToCentreAround = parent;
            options.escapeKeyTriggersCloseButton = true;
            options.useNativeTitleBar = true;
            options.resizable = true;

            // launchAsync() enters a modal state, so this stacks correctly on top of an already-modal
            // parent. Topmost handling is JUCE's: DefaultDialogWindow's constructor does
            // setAlwaysOnTop(areThereAnyAlwaysOnTopWindows()), so this inherits it from a topmost parent
            // without forcing it over unrelated applications when there is none.
            options.launchAsync();
        }

    } // namespace ui
} // namespace jucyaudio
