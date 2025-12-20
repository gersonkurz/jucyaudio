#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <filesystem>
#include <vector>
#include <functional>

namespace jucyaudio
{
    namespace ui
    {
        /**
         * @brief Dialog for database maintenance options
         *
         * Allows users to:
         * - Backup database before maintenance
         * - Restore database from a backup
         * - Run performance operations (VACUUM, FTS rebuild, etc.)
         */
        class DatabaseMaintenanceDialog : public juce::Component,
                                          public juce::Button::Listener
        {
        public:
            struct BackupEntry
            {
                std::filesystem::path path;
                std::string displayName;  // e.g., "01-2025-12-20"
            };

            struct Options
            {
                bool backupBeforeMaintenance{false};
                bool restoreFromBackup{false};
                std::filesystem::path backupToRestore;
                bool runPerformanceOperations{true};
            };

            using CompletionCallback = std::function<void(bool accepted, const Options& options)>;

            DatabaseMaintenanceDialog(const std::vector<BackupEntry>& availableBackups,
                                      CompletionCallback onComplete);
            ~DatabaseMaintenanceDialog() override = default;

            void paint(juce::Graphics& g) override;
            void resized() override;
            void buttonClicked(juce::Button* button) override;

            static void show(juce::Component* parent, CompletionCallback onComplete);

        private:
            void closeDialog(bool accepted);
            void updateControlStates();

            std::vector<BackupEntry> m_backups;
            CompletionCallback m_onComplete;

            // UI Components
            juce::Label m_titleLabel;

            juce::ToggleButton m_backupCheckbox;
            juce::Label m_backupLabel;

            juce::ToggleButton m_restoreCheckbox;
            juce::Label m_restoreLabel;
            juce::ComboBox m_restoreComboBox;

            juce::ToggleButton m_performanceCheckbox;
            juce::Label m_performanceLabel;

            juce::Label m_noteLabel;

            juce::TextButton m_okButton{"OK"};
            juce::TextButton m_cancelButton{"Cancel"};

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DatabaseMaintenanceDialog)
        };

    } // namespace ui
} // namespace jucyaudio
