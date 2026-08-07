#include <UI/TrackDetailsDialog.h>
#include <Utils/UiUtils.h>

namespace jucyaudio
{
    namespace ui
    {
        TrackDetailsDialog::TrackDetailsDialog(std::vector<juce::String> pages)
            : m_pages{std::move(pages)}
        {
            if (m_pages.empty())
                m_pages.emplace_back("No details available.");

            m_text.setMultiLine(true);
            m_text.setReadOnly(true);          // still selectable + copyable
            m_text.setScrollbarsShown(true);
            m_text.setCaretVisible(false);
            m_text.setPopupMenuEnabled(true);  // right-click -> Copy
            m_text.setFont(juce::Font{juce::FontOptions{}.withHeight(14.0f)});
            addAndMakeVisible(m_text);

            m_position.setJustificationType(juce::Justification::centred);

            const bool multiPage = m_pages.size() > 1;
            m_prev.setVisible(multiPage);
            m_next.setVisible(multiPage);
            m_position.setVisible(multiPage);

            m_prev.onClick = [this]
            {
                if (m_index > 0)
                {
                    --m_index;
                    showPage();
                }
            };
            m_next.onClick = [this]
            {
                if (m_index + 1 < m_pages.size())
                {
                    ++m_index;
                    showPage();
                }
            };
            m_copy.onClick = [this] { copyTextToClipboard(m_text.getText()); };
            m_close.onClick = [this]
            {
                if (auto *window = findParentComponentOfClass<juce::DialogWindow>())
                    window->exitModalState(0);
            };

            addAndMakeVisible(m_position);
            addAndMakeVisible(m_prev);
            addAndMakeVisible(m_next);
            addAndMakeVisible(m_copy);
            addAndMakeVisible(m_close);

            showPage();
            setSize(560, 440);
        }

        void TrackDetailsDialog::showPage()
        {
            m_text.setText(m_pages[m_index], false);
            m_position.setText(juce::String(static_cast<int>(m_index) + 1) + " of " + juce::String(static_cast<int>(m_pages.size())),
                               juce::dontSendNotification);
            m_prev.setEnabled(m_index > 0);
            m_next.setEnabled(m_index + 1 < m_pages.size());
        }

        void TrackDetailsDialog::resized()
        {
            auto bounds = getLocalBounds().reduced(10);

            auto buttonRow = bounds.removeFromBottom(30);
            bounds.removeFromBottom(8);
            m_text.setBounds(bounds);

            // Left: navigation; right: copy/close; centre: "N of M".
            m_prev.setBounds(buttonRow.removeFromLeft(80));
            buttonRow.removeFromLeft(6);
            m_next.setBounds(buttonRow.removeFromLeft(80));
            m_close.setBounds(buttonRow.removeFromRight(80));
            buttonRow.removeFromRight(6);
            m_copy.setBounds(buttonRow.removeFromRight(80));
            m_position.setBounds(buttonRow);
        }
    } // namespace ui
} // namespace jucyaudio
