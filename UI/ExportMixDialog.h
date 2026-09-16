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
        /// @brief The self test's way in. Declared here, defined in Tests/SelfTests.cpp.
        ///
        /// This dialog's behaviour lives in its private state - which fields still follow the mix name,
        /// what the rename commits - and it is driven by JUCE events on private members, so there is no
        /// checking it from outside without one of these. The alternative was to leave it to manual
        /// testing, which is what the first version of this feature did: the bug that hid there was a
        /// guard defeated by asynchronous notification, and it shipped looking like it worked.
        ///
        /// One forward declaration and one friend line is the whole cost in production code. Everything
        /// it grants access to stays in the test.
        struct ExportMixDialogTestAccess;

        class ExportMixDialog : public juce::Component,
                                public juce::Button::Listener,
                                public juce::TextEditor::Listener,
                                public juce::FilenameComponentListener,
                                public juce::ComboBox::Listener
        {
        public:
            enum class Result { Cancelled, ExportNow, ScheduleForLater };
            /// @param mixInfo The mix as it stands when the dialog closes, which is not necessarily
            ///        the one it was given: this dialog can rename the mix, and the caller needs the
            ///        new name for the export it is about to run, for what it logs, and for the
            ///        navigation node, which otherwise keeps the old name until something refreshes it.
            using OnExportCallback = std::function<void(Result result, const database::MixInfo &mixInfo, const audio::ActiveExportSettings &settings)>;

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
            friend struct ExportMixDialogTestAccess;

            /// @brief The leading number of a mix name, or empty. "4025 - Automix" gives "4025".
            ///
            /// Static because two paths share it - the defaults when the dialog opens, and the
            /// re-derivation when the mix is renamed in it. They agreed by accident before, when only
            /// one of them existed.
            static juce::String leadingTrackNumber(const juce::String &mixName);

            /// @brief The one name this dialog means when it says "the mix name".
            ///
            /// The editor's raw text is not it: a name typed with a stray space either side would be
            /// trimmed on the way to the database and left untrimmed everywhere it was derived from, so
            /// " 7 - Set " would save as "7 - Set" while the title kept its padding and the track number
            /// came out empty. Everything - the comparison that decides whether to rename, the
            /// validation, the rename itself, and all three derived fields - goes through here.
            static juce::String effectiveMixName(const juce::String &editorText);

            /// @brief Where an export of @p mixName under @p current would go.
            ///
            /// A mix name is free text and this turns it into a filename, so it has to assume the worst:
            /// "../elsewhere", "sub/dir/name" and an absolute path are all names somebody can type, and
            /// handing any of them to getChildFile puts the export somewhere the user did not choose.
            /// Scheduled exports make that worse, because nothing asks them to confirm an overwrite.
            ///
            /// @return The new file, or an empty File if the name cannot be made into one that stays in
            ///         @p current's directory - in which case the caller leaves the filename alone.
            static juce::File exportFileForName(const juce::File &current, const juce::String &mixName);

            void updateTagFieldsVisibility();
            void loadDefaultTags();

            /// @brief Re-derives everything this dialog takes from the mix name, skipping what the user
            ///        has since typed over.
            ///
            /// Three fields start life as a function of the name: the track title is the name, the track
            /// number is the number in front of it if there is one, and the output file is the name with
            /// an extension. Renaming the mix here has to carry them along or the rename is half done -
            /// that is the whole reason for renaming before exporting in the first place. It must not
            /// carry along a field the user has edited, which is what the m_*Edited flags are for.
            void applyMixNameDefaults(const juce::String &mixName);

            /// @brief Commits a rename, if the name was changed. Called from the export path only.
            ///
            /// Decides and writes; it does not report. The message box lives in the caller, because a
            /// function that both makes a decision and puts a window on the screen cannot be driven
            /// from a headless check without one appearing - and a modal one there has nobody to
            /// dismiss it. That is not hypothetical: the first version of this raised the alert from in
            /// here and hung the self test.
            ///
            /// @param errorOut Receives what to tell the user, when the answer is false.
            /// @return False if the rename was needed and did not happen; the caller then stops.
            bool commitMixNameIfChanged(std::string &errorOut);
            void populateExportFolders();
            void handleNewFolder();
            void handleExport();
            void handleCancel();
            void closeDialog(Result result);
            
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

            // The mix's own name, which is not an ID3 tag - it renames the mix in the library.
            juce::Label m_mixNameLabel;
            juce::TextEditor m_mixNameEditor;

            // Whether the user has taken over a field that would otherwise follow the mix name.
            bool m_trackTitleEdited{false};
            bool m_trackNumberEdited{false};
            bool m_outputFileEdited{false};

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
            
            // Schedule option
            juce::ToggleButton m_scheduleCheckbox{"Schedule for later"};

            // Buttons
            juce::TextButton m_exportButton;
            juce::TextButton m_cancelButton;
            
            juce::LookAndFeel_V4 m_lookAndFeel;
            
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportMixDialog)
        };
        
    } // namespace ui
} // namespace jucyaudio