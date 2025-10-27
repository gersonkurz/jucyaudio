#pragma once

#include <Database/Includes/MixInfo.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <filesystem>
#include <functional>
#include <Audio/Includes/ActiveExportSettings.h>

namespace jucyaudio
{
    namespace ui
    {
        class ExportMixDialog : public juce::Component,
                                public juce::Button::Listener,
                                public juce::TextEditor::Listener,
                                public juce::FilenameComponentListener,
                                public juce::ComboBox::Listener
        {
        public:
            using OnExportCallback = std::function<void(bool success, const audio::ActiveExportSettings& settings)>;
            
            ExportMixDialog(const database::MixInfo& mixInfo, OnExportCallback callback);
            ~ExportMixDialog() override;
            
            void paint(juce::Graphics& g) override;
            void resized() override;
            
            // Button::Listener
            void buttonClicked(juce::Button* button) override;
            
            // TextEditor::Listener
            void textEditorTextChanged(juce::TextEditor& editor) override;
            void textEditorReturnKeyPressed(juce::TextEditor& editor) override;
            void textEditorEscapeKeyPressed(juce::TextEditor& editor) override;
            void textEditorFocusLost(juce::TextEditor& editor) override {}
            
            // FilenameComponentListener
            void filenameComponentChanged(juce::FilenameComponent* component) override;

            // ComboBox::Listener
            void comboBoxChanged(juce::ComboBox* comboBox) override;

        private:
            void updateTagFieldsVisibility();
            void loadDefaultTags();
            void populateExportFolders();
            void handleNewFolder();
            void handleExport();
            void handleCancel();
            void closeDialog(bool success);
            
            database::MixInfo m_mixInfo;
            OnExportCallback m_callback;
            audio::ActiveExportSettings m_settings;
            
            // UI Components
            juce::Label m_titleLabel;
            
            // File selection
            juce::Label m_fileLabel;
            std::unique_ptr<juce::FilenameComponent> m_filenameComponent;

            // Export folder selection
            juce::Label m_exportFolderLabel;
            juce::ComboBox m_exportFolderCombo;
            juce::TextButton m_newFolderButton;
            
            // ID3 tag fields (only visible for MP3)
            juce::Label m_tagsHeaderLabel;
            
            juce::Label m_artistLabel;
            juce::TextEditor m_artistEditor;
            
            juce::Label m_albumLabel;
            juce::TextEditor m_albumEditor;
            
            juce::Label m_trackTitleLabel;
            juce::TextEditor m_trackTitleEditor;

            juce::Label m_trackNumberLabel;
            juce::TextEditor m_trackNumberEditor;

            juce::Label m_yearLabel;
            juce::TextEditor m_yearEditor;
            
            juce::Label m_genreLabel;
            juce::TextEditor m_genreEditor;
            
            juce::Label m_commentLabel;
            juce::TextEditor m_commentEditor;
            
            // Buttons
            juce::TextButton m_exportButton;
            juce::TextButton m_cancelButton;
            
            juce::LookAndFeel_V4 m_lookAndFeel;
            
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportMixDialog)
        };
        
    } // namespace ui
} // namespace jucyaudio