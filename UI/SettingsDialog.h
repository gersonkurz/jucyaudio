#pragma once
#include <UI/Settings.h>
#include <UI/SingletonDialog.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace jucyaudio
{
    namespace ui
    {
        /**
         * @brief Tab component for configuring MP3 export tags
         */
        class ExportSettingsTab : public juce::Component
        {
        public:
            ExportSettingsTab();
            ~ExportSettingsTab() override = default;

            void resized() override;

            // Save settings to config
            void saveSettings();

            // Load settings from config
            void loadSettings();

            // Override to fix light theme text colors
            void parentHierarchyChanged() override;

        private:
            juce::Label m_headerLabel{"header", "Default MP3 Export Tags"};

            juce::Label m_artistLabel{"artistLabel", "Artist:"};
            juce::TextEditor m_artistEditor;

            juce::Label m_albumLabel{"albumLabel", "Album:"};
            juce::TextEditor m_albumEditor;

            juce::Label m_yearLabel{"yearLabel", "Year:"};
            juce::TextEditor m_yearEditor;

            juce::Label m_genreLabel{"genreLabel", "Genre:"};
            juce::TextEditor m_genreEditor;

            juce::Label m_commentLabel{"commentLabel", "Comment:"};
            juce::TextEditor m_commentEditor;

            juce::Label m_noteLabel{"noteLabel", "Note: These are default values. You can override them when exporting individual mixes."};

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportSettingsTab)
        };

        /**
         * @brief Tab component for general and advanced application settings
         */
        class GeneralSettingsTab : public juce::Component
        {
        public:
            GeneralSettingsTab();
            ~GeneralSettingsTab() override = default;

            void resized() override;
            void saveSettings();
            void loadSettings();
            void parentHierarchyChanged() override;

        private:
            // Backup Settings
            juce::Label m_backupLabel;
            juce::Slider m_backupSlider;
            juce::Label m_backupSliderLabel;

            // Mix Editing Settings
            juce::Label m_mixEditingLabel;
            juce::ToggleButton m_removeFromWsToggle;
            juce::ToggleButton m_askBeforeRemovingToggle;
            juce::ToggleButton m_clearWsAfterExportToggle;
            juce::ToggleButton m_linkEnvelopePointsToggle;
            juce::ToggleButton m_useSmartAutomixToggle;
            juce::Label m_smartAutomixMaxSearchLabel;
            juce::Slider m_smartAutomixMaxSearchSlider;
            juce::Label m_removeTrackOptionLabel;
            juce::ComboBox m_removeTrackOptionCombo;

            // Logging Settings
            juce::Label m_loggingLabel;
            juce::Label m_logLevelLabel;
            juce::ComboBox m_logLevelCombo;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GeneralSettingsTab)
        };

        /**
         * @brief Tab component for plugin-related settings
         */
        class PluginSettingsTab : public juce::Component
        {
        public:
            PluginSettingsTab();
            ~PluginSettingsTab() override = default;

            void resized() override;
            void saveSettings();
            void loadSettings();

        private:
            juce::Label m_headerLabel{"header", "Plugin Scanning"};
            juce::Label m_pathsLabel{"pathsLabel", "VST3 scan paths (one per line):"};
            juce::TextEditor m_pathsEditor;
            juce::Label m_noteLabel{"noteLabel", "Leave empty to use JUCE defaults."};

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginSettingsTab)
        };

        /**
         * @brief Multi-tab settings dialog for JucyAudio
         */
        class SettingsDialog : public SingletonDialog<SettingsDialog>
        {
        public:
            SettingsDialog();
            ~SettingsDialog() override = default;

            void initializeContent();

            static void showSettingsDialog(juce::Component *centreAroundComponent = nullptr);

        private:
            class SettingsComponent : public juce::Component
            {
            public:
                SettingsComponent();
                ~SettingsComponent() override = default;

                void resized() override;
                void saveAllSettings();

            private:
                juce::TabbedComponent m_tabbedComponent{juce::TabbedButtonBar::TabsAtTop};

                std::unique_ptr<GeneralSettingsTab> m_generalTab;
                std::unique_ptr<ExportSettingsTab> m_exportTab;
                std::unique_ptr<PluginSettingsTab> m_pluginTab;

                juce::TextButton m_saveButton{"Save"};
                juce::TextButton m_cancelButton{"Cancel"};

                JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
            };

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsDialog)
        };
    } // namespace ui
} // namespace jucyaudio
