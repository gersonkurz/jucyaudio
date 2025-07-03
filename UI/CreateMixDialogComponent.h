#pragma once

#include <Database/Includes/Constants.h> // For jucyaudio::database::MixId
#include <Database/Includes/MixInfo.h>   // For jucyaudio::database::MixId
#include <Database/Includes/TrackInfo.h> // For jucyaudio::database::TrackInfo
#include <Database/TrackLibrary.h>       // For jucyaudio::TrackLibrary
#include <Audio/AudioLibrary.h>         // For jucyaudio::AudioLibrary
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <filesystem>
#include <functional>
#include <vector>

namespace jucyaudio
{
    namespace ui
    {
        class CreateMixDialogComponent : public juce::Component, public juce::Button::Listener, public juce::TextEditor::Listener
        {
        public:
            using OnMixCreatedAndExportedCallback = std::function<void(bool /*success*/, MixId /*newMixId*/)>;

            CreateMixDialogComponent(audio::AudioLibrary &audioLibrary, database::TrackLibrary &trackLibrary,
                                     const std::vector<database::TrackInfo> &tracksForMix, OnMixCreatedAndExportedCallback onOkCallback);
            ~CreateMixDialogComponent() override;

            void paint(juce::Graphics &g) override;
            void resized() override;

            // Button::Listener
            void buttonClicked(juce::Button *button) override;

            // TextEditor::Listener
            void textEditorReturnKeyPressed(juce::TextEditor &editor) override;
            void textEditorFocusLost([[maybe_unused]] juce::TextEditor &editor) override {}; // Required by TextEditor::Listener

            bool keyPressed(const juce::KeyPress &key) override;

        private:
            void closeThisDialog(bool success);
            void handleCreateAndExport();
            void handleCancel();
            juce::String generateDefaultMixName();
            void launchExportFileChooserAndProcess(MixId mixId, const juce::String &mixName); // Renamed for clarity
            void onFileChooserModalDismissed(const juce::FileChooser &chooser,
                                             MixId mixId,        // Pass mixId
                                             const juce::String &mixName); // Pass mixName

            audio::AudioLibrary &m_audioLibrary;
            database::TrackLibrary &m_trackLibrary;
            std::vector<database::TrackInfo> m_tracksForMix; // Store as reference
            OnMixCreatedAndExportedCallback m_onOkCallback;

            // *** Member to hold the FileChooser during async operation ***
            std::unique_ptr<juce::FileChooser> m_activeFileChooser;

            // UI Elements
            juce::Label m_titleLabel;
            juce::Label m_countLabel;
            juce::Label m_nameLabel;
            juce::TextEditor m_nameEditor;
            juce::TextButton m_okButton; // "Create & Export"
            juce::TextButton m_cancelButton;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CreateMixDialogComponent)
        };

    } // namespace ui
} // namespace jucyaudio