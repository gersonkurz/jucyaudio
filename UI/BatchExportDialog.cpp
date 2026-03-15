#include <UI/BatchExportDialog.h>
#include <UI/ThemeManager.h>
#include <Database/TrackLibrary.h>
#include <Utils/AssortedUtils.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        BatchExportDialog::BatchExportDialog(OnBatchExportCallback callback)
            : m_callback{std::move(callback)},
              m_titleLabel{"titleLabel", "Scheduled Exports"}
        {
            theThemeManager.applyCurrentTheme(m_lookAndFeel, this);

            m_titleLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(20.0f)}.boldened());
            m_titleLabel.setJustificationType(juce::Justification::left);
            addAndMakeVisible(m_titleLabel);

            m_listBox.setModel(&m_listModel);
            m_listBox.setMultipleSelectionEnabled(false);
            m_listBox.setRowHeight(24);
            addAndMakeVisible(m_listBox);

            addAndMakeVisible(m_removeButton);
            addAndMakeVisible(m_exportAllButton);
            addAndMakeVisible(m_cancelButton);
            m_removeButton.addListener(this);
            m_exportAllButton.addListener(this);
            m_cancelButton.addListener(this);

            refreshList();

            setSize(500, 400);
        }

        BatchExportDialog::~BatchExportDialog()
        {
            setLookAndFeel(nullptr);
        }

        void BatchExportDialog::paint(juce::Graphics& g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        }

        void BatchExportDialog::resized()
        {
            auto area = getLocalBounds().reduced(15);

            m_titleLabel.setBounds(area.removeFromTop(30));
            area.removeFromTop(10);

            auto buttonArea = area.removeFromBottom(35);
            area.removeFromBottom(10);

            m_listBox.setBounds(area);

            // Buttons: [Remove]  ...  [Export All] [Close]
            const int buttonWidth = 100;
            const int spacing = 10;

            m_removeButton.setBounds(buttonArea.removeFromLeft(buttonWidth));

            m_cancelButton.setBounds(buttonArea.removeFromRight(buttonWidth));
            buttonArea.removeFromRight(spacing);
            m_exportAllButton.setBounds(buttonArea.removeFromRight(buttonWidth));
        }

        void BatchExportDialog::buttonClicked(juce::Button* button)
        {
            if (button == &m_removeButton)
                handleRemoveSelected();
            else if (button == &m_exportAllButton)
                handleExportAll();
            else if (button == &m_cancelButton)
                handleCancel();
        }

        void BatchExportDialog::refreshList()
        {
            m_scheduled = database::theTrackLibrary.getMixManager().getMixesScheduledForExport();
            m_listBox.updateContent();
            m_listBox.repaint();

            m_exportAllButton.setEnabled(!m_scheduled.empty());
            m_removeButton.setEnabled(!m_scheduled.empty());

            m_titleLabel.setText(
                juce::String{"Scheduled Exports ("} + juce::String{static_cast<int>(m_scheduled.size())} + ")",
                juce::dontSendNotification);
        }

        void BatchExportDialog::handleRemoveSelected()
        {
            const auto selectedRow = m_listBox.getSelectedRow();
            if (selectedRow < 0 || selectedRow >= static_cast<int>(m_scheduled.size()))
                return;

            const auto& entry = m_scheduled[static_cast<size_t>(selectedRow)];
            database::theTrackLibrary.getMixManager().clearPendingExportSettings(entry.mixInfo.mixId);
            spdlog::info("Unscheduled mix '{}' from export", entry.mixInfo.name);
            refreshList();
        }

        void BatchExportDialog::handleExportAll()
        {
            if (m_scheduled.empty())
                return;

            // Hand the list to the caller and close
            if (m_callback)
            {
                m_callback(std::move(m_scheduled));
                m_callback = nullptr;
            }
            closeDialog();
        }

        void BatchExportDialog::handleCancel()
        {
            closeDialog();
        }

        void BatchExportDialog::closeDialog()
        {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            {
                dw->exitModalState(0);
            }
        }

        // --- ListModel ---

        int BatchExportDialog::ListModel::getNumRows()
        {
            return static_cast<int>(m_owner.m_scheduled.size());
        }

        void BatchExportDialog::ListModel::paintListBoxItem(
            int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
        {
            if (rowNumber < 0 || rowNumber >= static_cast<int>(m_owner.m_scheduled.size()))
                return;

            const auto& entry = m_owner.m_scheduled[static_cast<size_t>(rowNumber)];

            if (rowIsSelected)
                g.fillAll(juce::Colours::lightblue.withAlpha(0.3f));

            g.setColour(juce::Colours::white);
            g.setFont(14.0f);

            const auto text = juce::String{entry.mixInfo.name}
                + "  ->  " + juce::String{entry.settings.exportFolder}
                + "/" + juce::String{std::filesystem::path{entry.settings.outputPath}.filename().string()};

            g.drawText(text, 8, 0, width - 16, height, juce::Justification::centredLeft, true);
        }

    } // namespace ui
} // namespace jucyaudio
