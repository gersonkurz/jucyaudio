#pragma once

#include <Audio/AudioLibrary.h>          // For jucyaudio::AudioLibrary
#include <Database/Includes/Constants.h> // For jucyaudio::database::MixId
#include <Database/Includes/MixInfo.h>   // For jucyaudio::database::MixId
#include <Database/Includes/TrackInfo.h> // For jucyaudio::database::TrackInfo
#include <Database/TrackLibrary.h>       // For jucyaudio::TrackLibrary
#include <filesystem>
#include <functional>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

#undef EXPORT_FILE_FROM_CREATE_MIX_DIALOG

namespace jucyaudio
{
    namespace ui
    {
        class CreateMixTask final : public database::ILongRunningTask
        {
        public:
            CreateMixTask(const database::MixInfo& mixInfo, const audio::IMixExporter &mixExporter, 
                          const std::filesystem::path &targetExportPath);
            void run(database::ProgressCallback progressCb, database::CompletionCallback completionCb,
                     [[maybe_unused]] std::atomic<bool> &shouldCancel) override;

            bool m_bExported{false};

        private:
            MixId m_mixId;
            std::filesystem::path m_targetExportPath;
            const audio::IMixExporter &m_mixExporter;
        };

        class CreateMixDialogComponent : public juce::Component, public juce::Button::Listener, public juce::TextEditor::Listener
        {
        public:
            using OnMixCreatedAndExportedCallback = std::function<void(bool /*success*/, const database::MixInfo & /*newMixInfo */)>;

            CreateMixDialogComponent(audio::AudioLibrary &audioLibrary, 
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
            void handleCreateMix();
            void handleCancel();
            juce::String generateDefaultMixName();
#ifdef EXPORT_FILE_FROM_CREATE_MIX_DIALOG
            void launchExportFileChooserAndProcess(database::MixInfo mixInfo); // Renamed for clarity
            void onFileChooserModalDismissed(const juce::FileChooser &chooser, database::MixInfo mixInfo);
#endif

            audio::AudioLibrary &m_audioLibrary;
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
            juce::LookAndFeel_V4 m_lookAndFeel; // Custom LookAndFeel instance

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CreateMixDialogComponent)
        };

    } // namespace ui
} // namespace jucyaudio