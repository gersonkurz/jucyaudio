#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <UI/Settings.h>
#include <UI/SingletonDialog.h>
#include <memory>

namespace jucyaudio::ui
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
     * @brief Tab component for general application settings
     */
    class GeneralSettingsTab : public juce::Component
    {
    public:
        GeneralSettingsTab();
        ~GeneralSettingsTab() override = default;
        
        void resized() override;
        void saveSettings();
        void loadSettings();
        
    private:
        juce::Label m_headerLabel{"header", "General Settings"};
        juce::Label m_placeholderLabel{"placeholder", "General settings will be added here in future versions."};
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GeneralSettingsTab)
    };
    
    /**
     * @brief Multi-tab settings dialog for JucyAudio
     */
    class SettingsDialog : public SingletonDialog<SettingsDialog>
    {
    public:
        SettingsDialog();
        ~SettingsDialog() override = default;
        
        /**
         * @brief Initialize content after L&F has been set
         */
        void initializeContent();
        
        /**
         * @brief Shows the settings dialog
         * @param centreAroundComponent Component to center the dialog around
         */
        static void showSettingsDialog(juce::Component* centreAroundComponent = nullptr);
        
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
            
            juce::TextButton m_saveButton{"Save"};
            juce::TextButton m_cancelButton{"Cancel"};
            
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
        };
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsDialog)
    };
}