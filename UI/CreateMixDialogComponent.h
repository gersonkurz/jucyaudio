#pragma once

#include <Audio/AudioLibrary.h>
#include <Audio/Includes/ActiveExportSettings.h>
#include <Database/Includes/Constants.h>
#include <Database/Includes/MixInfo.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/TrackLibrary.h>
#include <filesystem>
#include <functional>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace jucyaudio
{
    namespace ui
    {

        class CreateMixDialogComponent : public juce::Component,
                                         public juce::Button::Listener,
                                         public juce::TextEditor::Listener,
                                         public juce::ComboBox::Listener
        {
        public:
            using OnMixCreatedAndExportedCallback = std::function<void(bool /*success*/, const database::MixInfo & /*newMixInfo */)>;

            CreateMixDialogComponent(
                const std::vector<database::TrackInfo> &tracksForMix,
                WorkingSetId source_ws_id,
                OnMixCreatedAndExportedCallback onOkCallback);
            ~CreateMixDialogComponent() override;

            void paint(juce::Graphics &g) override;
            void resized() override;

            // Button::Listener
            void buttonClicked(juce::Button *button) override;

            // TextEditor::Listener
            void textEditorReturnKeyPressed(juce::TextEditor &editor) override;
            void textEditorFocusLost([[maybe_unused]] juce::TextEditor &editor) override {}; // Required by TextEditor::Listener

            // ComboBox::Listener
            void comboBoxChanged(juce::ComboBox *comboBox) override;

            bool keyPressed(const juce::KeyPress &key) override;

        private:
            void closeThisDialog(bool success);
            void handleCreateMix();
            void handleCancel();
            void finishMixCreation(const std::string& mixName);
            void finishAppendToMix(const database::MixInfo& targetMix);
            juce::String generateDefaultMixName();

            std::vector<database::TrackInfo> m_tracksForMix; // Store as reference
            WorkingSetId m_source_ws_id;
            OnMixCreatedAndExportedCallback m_onOkCallback;

            // *** Member to hold the FileChooser during async operation ***
            std::unique_ptr<juce::FileChooser> m_activeFileChooser;

            // UI Elements
            juce::Label m_titleLabel;
            juce::Label m_countLabel;
            juce::Label m_mixSelectLabel;
            juce::ComboBox m_mixSelectCombo;
            juce::Label m_nameLabel;
            juce::TextEditor m_nameEditor;
            juce::TextButton m_okButton;
            juce::TextButton m_cancelButton;
            juce::LookAndFeel_V4 m_lookAndFeel;

            // Store available mixes
            std::vector<database::MixInfo> m_availableMixes;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CreateMixDialogComponent)
        };

    } // namespace ui
} // namespace jucyaudio
