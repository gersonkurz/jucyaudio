#include "DatabaseMaintenanceDialog.h"
#include <Database/DatabaseBackupManager.h>
#include <Utils/AssortedUtils.h>
#include <algorithm>
#include <regex>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        DatabaseMaintenanceDialog::DatabaseMaintenanceDialog(
            const std::vector<BackupEntry>& availableBackups,
            CompletionCallback onComplete)
            : m_backups{availableBackups}
            , m_onComplete{std::move(onComplete)}
        {
            // Title
            m_titleLabel.setText("Database Maintenance Options", juce::dontSendNotification);
            m_titleLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(18.0f)}.boldened());
            m_titleLabel.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(m_titleLabel);

            // Backup before maintenance
            m_backupCheckbox.setToggleState(false, juce::dontSendNotification);
            m_backupCheckbox.onClick = [this]() { updateControlStates(); };
            addAndMakeVisible(m_backupCheckbox);

            m_backupLabel.setText("Backup database before maintenance", juce::dontSendNotification);
            addAndMakeVisible(m_backupLabel);

            // Restore from backup
            m_restoreCheckbox.setToggleState(false, juce::dontSendNotification);
            m_restoreCheckbox.onClick = [this]() { updateControlStates(); };
            addAndMakeVisible(m_restoreCheckbox);

            m_restoreLabel.setText("Restore database from backup:", juce::dontSendNotification);
            addAndMakeVisible(m_restoreLabel);

            // Populate restore combo box
            m_restoreComboBox.setEnabled(false);
            if (m_backups.empty())
            {
                m_restoreComboBox.addItem("(No backups available)", 1);
                m_restoreCheckbox.setEnabled(false);
            }
            else
            {
                int itemId = 1;
                for (const auto& backup : m_backups)
                {
                    m_restoreComboBox.addItem(backup.displayName, itemId++);
                }
                m_restoreComboBox.setSelectedId(1, juce::dontSendNotification);
            }
            addAndMakeVisible(m_restoreComboBox);

            // Performance operations
            m_performanceCheckbox.setToggleState(true, juce::dontSendNotification);
            m_performanceCheckbox.onClick = [this]() { updateControlStates(); };
            addAndMakeVisible(m_performanceCheckbox);

            m_performanceLabel.setText("Run performance operations (VACUUM, rebuild indexes)", juce::dontSendNotification);
            addAndMakeVisible(m_performanceLabel);

            // Note label
            m_noteLabel.setText("Note: Restore will only restore the database; other options will be ignored.",
                               juce::dontSendNotification);
            m_noteLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(12.0f)}.italicised());
            m_noteLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
            addAndMakeVisible(m_noteLabel);

            // Buttons
            m_okButton.addListener(this);
            addAndMakeVisible(m_okButton);

            m_cancelButton.addListener(this);
            addAndMakeVisible(m_cancelButton);

            updateControlStates();
            setSize(450, 280);
        }

        void DatabaseMaintenanceDialog::paint(juce::Graphics& g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        }

        void DatabaseMaintenanceDialog::resized()
        {
            auto bounds = getLocalBounds().reduced(20);

            // Title
            m_titleLabel.setBounds(bounds.removeFromTop(30));
            bounds.removeFromTop(15);

            // Backup checkbox row
            auto backupRow = bounds.removeFromTop(25);
            m_backupCheckbox.setBounds(backupRow.removeFromLeft(25));
            m_backupLabel.setBounds(backupRow);
            bounds.removeFromTop(10);

            // Restore checkbox row
            auto restoreRow = bounds.removeFromTop(25);
            m_restoreCheckbox.setBounds(restoreRow.removeFromLeft(25));
            m_restoreLabel.setBounds(restoreRow);
            bounds.removeFromTop(5);

            // Restore combo box (indented)
            auto comboRow = bounds.removeFromTop(25);
            comboRow.removeFromLeft(35);
            m_restoreComboBox.setBounds(comboRow.removeFromLeft(300));
            bounds.removeFromTop(10);

            // Performance checkbox row
            auto perfRow = bounds.removeFromTop(25);
            m_performanceCheckbox.setBounds(perfRow.removeFromLeft(25));
            m_performanceLabel.setBounds(perfRow);
            bounds.removeFromTop(10);

            // Note
            m_noteLabel.setBounds(bounds.removeFromTop(40));
            bounds.removeFromTop(10);

            // Buttons
            auto buttonArea = bounds.removeFromBottom(30);
            m_cancelButton.setBounds(buttonArea.removeFromRight(80));
            buttonArea.removeFromRight(10);
            m_okButton.setBounds(buttonArea.removeFromRight(80));
        }

        void DatabaseMaintenanceDialog::buttonClicked(juce::Button* button)
        {
            if (button == &m_okButton)
            {
                closeDialog(true);
            }
            else if (button == &m_cancelButton)
            {
                closeDialog(false);
            }
        }

        void DatabaseMaintenanceDialog::updateControlStates()
        {
            const bool restoreSelected = m_restoreCheckbox.getToggleState();

            // Enable/disable combo box based on restore checkbox
            m_restoreComboBox.setEnabled(restoreSelected && !m_backups.empty());

            // If restore is selected, disable other options (they won't be used)
            m_backupCheckbox.setEnabled(!restoreSelected);
            m_performanceCheckbox.setEnabled(!restoreSelected);

            // Grey out labels when disabled
            const auto normalColor = getLookAndFeel().findColour(juce::Label::textColourId);
            const auto disabledColor = juce::Colours::grey;

            m_backupLabel.setColour(juce::Label::textColourId, restoreSelected ? disabledColor : normalColor);
            m_performanceLabel.setColour(juce::Label::textColourId, restoreSelected ? disabledColor : normalColor);
        }

        void DatabaseMaintenanceDialog::closeDialog(bool accepted)
        {
            Options options;

            if (accepted)
            {
                options.restoreFromBackup = m_restoreCheckbox.getToggleState();

                if (options.restoreFromBackup && !m_backups.empty())
                {
                    int selectedIdx = m_restoreComboBox.getSelectedId() - 1;
                    if (selectedIdx >= 0 && selectedIdx < static_cast<int>(m_backups.size()))
                    {
                        options.backupToRestore = m_backups[selectedIdx].path;
                    }
                    // When restoring, ignore other options
                    assert(options.backupBeforeMaintenance == false);
                    options.runPerformanceOperations = false;
                }
                else
                {
                    options.backupBeforeMaintenance = m_backupCheckbox.getToggleState();
                    options.runPerformanceOperations = m_performanceCheckbox.getToggleState();
                }
            }

            if (m_onComplete)
            {
                m_onComplete(accepted, options);
            }

            // Close the dialog window
            if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            {
                window->exitModalState(accepted ? 1 : 0);
            }
        }

        void DatabaseMaintenanceDialog::show(juce::Component* parent, CompletionCallback onComplete)
        {
            // Get list of available backups
            std::vector<BackupEntry> backups;

            // Get database path from config root
            auto configRoot = getConfigRoot();
            auto dbDir = configRoot;

            spdlog::debug("Looking for backups in: {}", dbDir.string());

            if (std::filesystem::exists(dbDir))
            {
                const std::regex backupRegex(R"((\d{2})-(\d{4}-\d{2}-\d{2})\.sqlite)");

                for (const auto& entry : std::filesystem::directory_iterator(dbDir))
                {
                    if (entry.is_regular_file())
                    {
                        const std::string filename = entry.path().filename().string();
                        if (std::regex_match(filename, backupRegex))
                        {
                            backups.emplace_back(BackupEntry{entry.path(), filename});
                        }
                    }
                }

                // Sort by filename (which includes date) in descending order (newest first)
                std::sort(backups.begin(), backups.end(),
                    [](const BackupEntry& a, const BackupEntry& b) {
                        return a.displayName > b.displayName;
                    });
            }

            spdlog::info("Found {} backup files", backups.size());

            // Create and show dialog
            auto* dialog = new DatabaseMaintenanceDialog(backups, std::move(onComplete));

            juce::DialogWindow::LaunchOptions options;
            options.dialogTitle = "Database Maintenance";
            options.content.setOwned(dialog);
            options.componentToCentreAround = parent;
            options.escapeKeyTriggersCloseButton = true;
            options.useNativeTitleBar = true;
            options.resizable = false;

            options.launchAsync();
        }

    } // namespace ui
} // namespace jucyaudio
