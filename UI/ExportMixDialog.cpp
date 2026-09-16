#include <UI/ExportMixDialog.h>
#include <UI/Settings.h>
#include <UI/ThemeManager.h>
#include <Database/TrackLibrary.h>
#include <Database/Includes/IMixManager.h>
#include <Utils/UiUtils.h>
#include <format>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        ExportMixDialog::ExportMixDialog(const database::MixInfo &mixInfo, OnExportCallback callback)
            : m_mixInfo{mixInfo},
              m_callback{callback},
              m_titleLabel{"titleLabel", std::format("Export Mix: {}", mixInfo.name)},
              m_fileLabel{"fileLabel", "Output File:"},
              m_exportFolderLabel{"exportFolderLabel", "Export To Folder:"},
              m_newFolderButton{"New Folder..."},
              m_mixNameLabel{"mixNameLabel", "Mix Name:"},
              m_tagsHeaderLabel{"tagsHeader", "ID3 Tags (MP3 only):"},
              m_artistLabel{"artistLabel", "Artist:"},
              m_albumLabel{"albumLabel", "Album:"},
              m_trackTitleLabel{"trackTitleLabel", "Title:"},
              m_trackNumberLabel{"trackNumberLabel", "Track #:"},
              m_yearLabel{"yearLabel", "Year:"},
              m_genreLabel{"genreLabel", "Genre:"},
              m_commentLabel{"commentLabel", "Comment:"},
              m_exportButton{"Export"},
              m_cancelButton{"Cancel"}
        {
            theThemeManager.applyCurrentTheme(m_lookAndFeel, this);

            // Title
            addAndMakeVisible(m_titleLabel);
            m_titleLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(20.0f)}.boldened());
            m_titleLabel.setJustificationType(juce::Justification::left);

            // Export folder selection (MUST come before file selection)
            addAndMakeVisible(m_exportFolderLabel);
            addAndMakeVisible(m_exportFolderCombo);
            m_exportFolderCombo.setTextWhenNothingSelected("Select export folder...");
            m_exportFolderCombo.setTextWhenNoChoicesAvailable("No folders available");
            m_exportFolderCombo.addListener(this);

            addAndMakeVisible(m_newFolderButton);
            m_newFolderButton.addListener(this);

            // Populate the folder list
            populateExportFolders();

            // File selection
            addAndMakeVisible(m_fileLabel);

            // Create filename component
            m_filenameComponent = std::make_unique<juce::FilenameComponent>("exportFile",
                juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(mixInfo.name + ".mp3"),
                false,               // canEditFilename
                false,               // isDirectory
                true,                // isSaving
                "*.mp3;*.wav;*.m3u", // fileBrowserWildcard
                "",                  // enforcedSuffix
                "Choose export location");

            m_filenameComponent->addListener(this);
            addAndMakeVisible(m_filenameComponent.get());

            // The mix name. Above the tags rather than among them, because it is not one: editing it
            // renames the mix in the library, which outlives the file being written here.
            addAndMakeVisible(m_mixNameLabel);
            addAndMakeVisible(m_mixNameEditor);
            m_mixNameEditor.setText(mixInfo.name, juce::dontSendNotification);
            m_mixNameEditor.addListener(this);

            // ID3 tag fields
            addAndMakeVisible(m_tagsHeaderLabel);
            m_tagsHeaderLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(16.0f)}.boldened());

            // Artist
            addAndMakeVisible(m_artistLabel);
            addAndMakeVisible(m_artistEditor);
            m_artistEditor.addListener(this);

            // Album
            addAndMakeVisible(m_albumLabel);
            addAndMakeVisible(m_albumEditor);
            m_albumEditor.addListener(this);

            // Title
            addAndMakeVisible(m_trackTitleLabel);
            addAndMakeVisible(m_trackTitleEditor);
            m_trackTitleEditor.addListener(this);

            // Track Number
            addAndMakeVisible(m_trackNumberLabel);
            addAndMakeVisible(m_trackNumberEditor);
            m_trackNumberEditor.addListener(this);

            // Year
            addAndMakeVisible(m_yearLabel);
            addAndMakeVisible(m_yearEditor);
            m_yearEditor.addListener(this);

            // Genre
            addAndMakeVisible(m_genreLabel);
            addAndMakeVisible(m_genreEditor);
            m_genreEditor.addListener(this);

            // Comment
            addAndMakeVisible(m_commentLabel);
            addAndMakeVisible(m_commentEditor);
            m_commentEditor.setMultiLine(true);
            m_commentEditor.setReturnKeyStartsNewLine(true);
            m_commentEditor.addListener(this);

            // Schedule checkbox
            addAndMakeVisible(m_scheduleCheckbox);
            m_scheduleCheckbox.addListener(this);

            // Buttons
            addAndMakeVisible(m_exportButton);
            addAndMakeVisible(m_cancelButton);
            m_exportButton.addListener(this);
            m_cancelButton.addListener(this);

            // Load default values
            loadDefaultTags();

            // Pre-populate from saved pending export settings if available
            const auto pending = database::theTrackLibrary.getMixManager().getPendingExportSettings(mixInfo.mixId);
            if (pending.has_value())
            {
                const auto& s = *pending;
                m_filenameComponent->setCurrentFile(juce::File{s.outputPath.string()}, true, juce::dontSendNotification);
                m_artistEditor.setText(s.artist);
                m_albumEditor.setText(s.album);
                m_trackTitleEditor.setText(s.title);
                m_trackNumberEditor.setText(s.trackNumber);
                m_yearEditor.setText(s.year);
                m_genreEditor.setText(s.genre);
                m_commentEditor.setText(s.comment);
                m_scheduleCheckbox.setToggleState(true, juce::dontSendNotification);
                m_exportButton.setButtonText("Schedule");

                // Everything just restored was chosen deliberately last time, so renaming the mix now
                // must not overwrite any of it. Without this the three name-derived fields would look
                // untouched and a rename would quietly discard a pending export's settings.
                m_trackTitleEdited = true;
                m_trackNumberEdited = true;
                m_outputFileEdited = true;

                // Select the saved export folder
                if (!s.exportFolder.empty())
                {
                    for (int i = 0; i < m_exportFolderCombo.getNumItems(); ++i)
                    {
                        if (m_exportFolderCombo.getItemText(i) == juce::String{s.exportFolder})
                        {
                            m_exportFolderCombo.setSelectedItemIndex(i, juce::dontSendNotification);
                            break;
                        }
                    }
                }
            }

            // Set initial visibility based on file extension
            updateTagFieldsVisibility();

            // Set size after all components are created (increased for schedule checkbox, then again
            // for the mix name row: 25 for the row plus 20 of spacing below it)
            setSize(600, 625);

            // Set initial focus
            juce::Component::SafePointer<ExportMixDialog> safeThis = this;
            juce::MessageManager::callAsync(
                [safeThis]()
                {
                    if (safeThis && safeThis->isShowing())
                    {
                        safeThis->m_filenameComponent->grabKeyboardFocus();
                    }
                });
        }

        ExportMixDialog::~ExportMixDialog()
        {
            setLookAndFeel(nullptr);
        }

        void ExportMixDialog::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        }

        void ExportMixDialog::resized()
        {
            auto area = getLocalBounds().reduced(20);

            // Title
            m_titleLabel.setBounds(area.removeFromTop(30));
            area.removeFromTop(10);

            // Export folder selection
            m_exportFolderLabel.setBounds(area.removeFromTop(20));
            area.removeFromTop(5);
            auto folderRow = area.removeFromTop(25);
            m_newFolderButton.setBounds(folderRow.removeFromRight(100));
            folderRow.removeFromRight(10); // spacing
            m_exportFolderCombo.setBounds(folderRow);
            area.removeFromTop(15);

            // File selection
            m_fileLabel.setBounds(area.removeFromTop(20));
            area.removeFromTop(5);
            m_filenameComponent->setBounds(area.removeFromTop(25));
            area.removeFromTop(20);

            // Mix name
            auto mixNameRow = area.removeFromTop(25);
            m_mixNameLabel.setBounds(mixNameRow.removeFromLeft(80));
            m_mixNameEditor.setBounds(mixNameRow.reduced(2, 0));
            area.removeFromTop(20);

            // ID3 Tags section
            m_tagsHeaderLabel.setBounds(area.removeFromTop(25));
            area.removeFromTop(10);

            // Tag fields in two columns
            auto tagArea = area.removeFromTop(200);
            auto leftColumn = tagArea.removeFromLeft(tagArea.getWidth() / 2 - 5);
            auto rightColumn = tagArea;
            rightColumn.removeFromLeft(10); // spacing

            // Left column: Artist, Album, Title, Track #
            auto row = leftColumn.removeFromTop(25);
            m_artistLabel.setBounds(row.removeFromLeft(60));
            m_artistEditor.setBounds(row.reduced(2, 0));
            leftColumn.removeFromTop(10);

            row = leftColumn.removeFromTop(25);
            m_albumLabel.setBounds(row.removeFromLeft(60));
            m_albumEditor.setBounds(row.reduced(2, 0));
            leftColumn.removeFromTop(10);

            row = leftColumn.removeFromTop(25);
            m_trackTitleLabel.setBounds(row.removeFromLeft(60));
            m_trackTitleEditor.setBounds(row.reduced(2, 0));
            leftColumn.removeFromTop(10);

            row = leftColumn.removeFromTop(25);
            m_trackNumberLabel.setBounds(row.removeFromLeft(60));
            m_trackNumberEditor.setBounds(row.reduced(2, 0));

            // Right column: Year, Genre
            row = rightColumn.removeFromTop(25);
            m_yearLabel.setBounds(row.removeFromLeft(60));
            m_yearEditor.setBounds(row.reduced(2, 0));
            rightColumn.removeFromTop(10);

            row = rightColumn.removeFromTop(25);
            m_genreLabel.setBounds(row.removeFromLeft(60));
            m_genreEditor.setBounds(row.reduced(2, 0));

            // Comment (full width)
            area.removeFromTop(10);
            row = area.removeFromTop(20);
            m_commentLabel.setBounds(row);
            m_commentEditor.setBounds(area.removeFromTop(60));

            // Schedule checkbox
            area.removeFromTop(10);
            m_scheduleCheckbox.setBounds(area.removeFromTop(25));

            // Buttons at bottom
            auto buttonArea = getLocalBounds().removeFromBottom(40).reduced(20, 5);
            const int buttonWidth = 100;
            const int buttonSpacing = 10;

            m_cancelButton.setBounds(buttonArea.removeFromRight(buttonWidth));
            buttonArea.removeFromRight(buttonSpacing);
            m_exportButton.setBounds(buttonArea.removeFromRight(buttonWidth));
        }

        void ExportMixDialog::buttonClicked(juce::Button *button)
        {
            if (button == &m_exportButton)
            {
                handleExport();
            }
            else if (button == &m_cancelButton)
            {
                handleCancel();
            }
            else if (button == &m_newFolderButton)
            {
                handleNewFolder();
            }
            else if (button == &m_scheduleCheckbox)
            {
                m_exportButton.setButtonText(m_scheduleCheckbox.getToggleState() ? "Schedule" : "Export");
            }
        }

        void ExportMixDialog::textEditorTextChanged(juce::TextEditor &editor)
        {
            // The mix name is not one of the settings - it names the mix, not the file - so it is
            // handled first and separately. Everything it feeds is re-derived here rather than at
            // export time, so the user can see what renaming did before committing to it.
            if (&editor == &m_mixNameEditor)
            {
                applyMixNameDefaults(editor.getText());
                return;
            }

            // A field the user typed into stops following the name. No guard: everything this dialog
            // writes into these two goes in with dontSendNotification, so reaching here means the user
            // did it.
            if (&editor == &m_trackTitleEditor)
            {
                m_trackTitleEdited = true;
            }
            else if (&editor == &m_trackNumberEditor)
            {
                m_trackNumberEdited = true;
            }

            // Update settings as user types
            if (&editor == &m_artistEditor)
                m_settings.artist = editor.getText().toStdString();
            else if (&editor == &m_albumEditor)
                m_settings.album = editor.getText().toStdString();
            else if (&editor == &m_trackTitleEditor)
                m_settings.title = editor.getText().toStdString();
            else if (&editor == &m_trackNumberEditor)
                m_settings.trackNumber = editor.getText().toStdString();
            else if (&editor == &m_yearEditor)
                m_settings.year = editor.getText().toStdString();
            else if (&editor == &m_genreEditor)
                m_settings.genre = editor.getText().toStdString();
            else if (&editor == &m_commentEditor)
                m_settings.comment = editor.getText().toStdString();
        }

        void ExportMixDialog::textEditorReturnKeyPressed(juce::TextEditor &editor)
        {
            // Don't trigger export on return in the multiline comment field
            if (&editor != &m_commentEditor)
            {
                handleExport();
            }
        }

        void ExportMixDialog::textEditorEscapeKeyPressed(juce::TextEditor &editor)
        {
            handleCancel();
        }

        void ExportMixDialog::filenameComponentChanged(juce::FilenameComponent *component)
        {
            if (component == m_filenameComponent.get())
            {
                // The user has chosen where this goes, so renaming the mix no longer moves it.
                m_outputFileEdited = true;
                updateTagFieldsVisibility();
            }
        }

        void ExportMixDialog::comboBoxChanged(juce::ComboBox *comboBox)
        {
            if (comboBox == &m_exportFolderCombo)
            {
                // Auto-populate Album and Genre from export folder name
                const auto folderName = m_exportFolderCombo.getText();
                if (folderName.isNotEmpty())
                {
                    m_albumEditor.setText(folderName);
                    m_genreEditor.setText(folderName);

                    // Update settings
                    m_settings.genre = m_settings.album = folderName.toStdString();
                }
            }
        }

        void ExportMixDialog::updateTagFieldsVisibility()
        {
            const auto file = m_filenameComponent->getCurrentFile();
            const bool isMp3 = file.hasFileExtension(".mp3");

            // Show/hide tag fields based on file type
            m_tagsHeaderLabel.setVisible(isMp3);
            m_artistLabel.setVisible(isMp3);
            m_artistEditor.setVisible(isMp3);
            m_albumLabel.setVisible(isMp3);
            m_albumEditor.setVisible(isMp3);
            m_trackTitleLabel.setVisible(isMp3);
            m_trackTitleEditor.setVisible(isMp3);
            m_trackNumberLabel.setVisible(isMp3);
            m_trackNumberEditor.setVisible(isMp3);
            m_yearLabel.setVisible(isMp3);
            m_yearEditor.setVisible(isMp3);
            m_genreLabel.setVisible(isMp3);
            m_genreEditor.setVisible(isMp3);
            m_commentLabel.setVisible(isMp3);
            m_commentEditor.setVisible(isMp3);

            if (isMp3)
            {
                // Repopulate m_settings from text editors when switching back to MP3
                m_settings.artist = m_artistEditor.getText().toStdString();
                m_settings.album = m_albumEditor.getText().toStdString();
                m_settings.title = m_trackTitleEditor.getText().toStdString();
                m_settings.trackNumber = m_trackNumberEditor.getText().toStdString();
                m_settings.year = m_yearEditor.getText().toStdString();
                m_settings.genre = m_genreEditor.getText().toStdString();
                m_settings.comment = m_commentEditor.getText().toStdString();
            }
            else
            {
                // Clear tag fields for non-MP3 exports
                m_settings.artist.clear();
                m_settings.album.clear();
                m_settings.title.clear();
                m_settings.trackNumber.clear();
                m_settings.year.clear();
                m_settings.genre.clear();
                m_settings.comment.clear();
            }
        }

        void ExportMixDialog::populateExportFolders()
        {
            m_exportFolderCombo.clear();

            // Get export folders from database
            const auto &mixManager = database::theTrackLibrary.getMixManager();
            const auto folders = mixManager.getExportFolders();

            // Add each folder to the combo box
            int id = 1;
            for (const auto &folder : folders)
            {
                m_exportFolderCombo.addItem(folder.name, id++);
            }

            // Try to restore the last-used folder
            const auto lastUsedFolder = config::theSettings.exportSettings.lastUsedExportFolder.get();
            bool foundLastUsed = false;

            if (!lastUsedFolder.empty())
            {
                for (int i = 0; i < m_exportFolderCombo.getNumItems(); ++i)
                {
                    if (m_exportFolderCombo.getItemText(i) == juce::String{lastUsedFolder})
                    {
                        m_exportFolderCombo.setSelectedItemIndex(i);
                        foundLastUsed = true;
                        break;
                    }
                }
            }

            // If last-used folder wasn't found, select first folder as fallback
            if (!foundLastUsed && m_exportFolderCombo.getNumItems() > 0)
            {
                m_exportFolderCombo.setSelectedId(1);
            }
        }

        void ExportMixDialog::handleNewFolder()
        {
            juce::AlertWindow dialog("Create Export Folder", "Enter name for new export folder:", juce::AlertWindow::NoIcon);
            dialog.addTextEditor("name", "", "Folder Name:");
            dialog.addButton("Create", 1, juce::KeyPress(juce::KeyPress::returnKey));
            dialog.addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            focusTextEditorOnOpen(dialog, "name");

            if (dialog.runModalLoop() == 1)
            {
                const auto folderName = dialog.getTextEditorContents("name").trim();
                if (folderName.isNotEmpty())
                {
                    const auto &mixManager = database::theTrackLibrary.getMixManager();
                    if (mixManager.createExportFolder(folderName.toStdString()))
                    {
                        // Refresh the list and select the new folder
                        populateExportFolders();

                        // Find and select the new folder
                        for (int i = 0; i < m_exportFolderCombo.getNumItems(); ++i)
                        {
                            if (m_exportFolderCombo.getItemText(i) == folderName)
                            {
                                m_exportFolderCombo.setSelectedItemIndex(i);
                                break;
                            }
                        }
                    }
                    else
                    {
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                            "Failed to Create Folder",
                            "Could not create export folder. It may already exist.");
                    }
                }
            }
        }

        juce::String ExportMixDialog::leadingTrackNumber(const juce::String &mixName)
        {
            // "4025 - Automix 2025-10-26" exports as track 4025. Anything else has no track number:
            // guessing one from a name that does not carry one would be worse than leaving it blank.
            const auto firstSpace = mixName.indexOfChar(' ');
            if (firstSpace <= 0)
            {
                return {};
            }

            const auto possibleNumber = mixName.substring(0, firstSpace);
            return possibleNumber.containsOnly("0123456789") ? possibleNumber : juce::String{};
        }

        juce::String ExportMixDialog::effectiveMixName(const juce::String &editorText)
        {
            return editorText.trim();
        }

        juce::File ExportMixDialog::exportFileForName(const juce::File &current, const juce::String &mixName)
        {
            if (current == juce::File{} || mixName.isEmpty())
            {
                return {};
            }

            // createLegalFileName is what turns free text into one filename component: it strips the
            // separators and the characters the platform refuses. It is not enough on its own - it
            // leaves ".." alone, and that is a legal filename that means something else - so the result
            // is checked against the directory it is supposed to be in afterwards.
            const auto extension = current.getFileExtension();
            const auto stem = juce::File::createLegalFileName(mixName).trim();
            if (stem.isEmpty() || stem == "." || stem == "..")
            {
                return {};
            }

            const auto parent = current.getParentDirectory();
            const auto candidate = parent.getChildFile(stem + extension);

            // The belt to that brace: whatever the name was, the file has to be directly in the folder
            // the user chose. isAChildOf would also accept a deeper path; getParentDirectory equality
            // will not.
            if (candidate.getParentDirectory() != parent)
            {
                return {};
            }
            return candidate;
        }

        void ExportMixDialog::applyMixNameDefaults(const juce::String &rawName)
        {
            const auto mixName = effectiveMixName(rawName);

            // Every write here is dontSendNotification, and that is load-bearing rather than tidy.
            //
            // juce::TextEditor::textChanged posts the listener call with postCommandMessage - it is
            // delivered later, not during setText. A flag set around the call has therefore already
            // been cleared by the time textEditorTextChanged runs, so a guard of that shape cannot tell
            // this dialog's writes from the user's. It silently marked both derived fields as
            // user-edited during construction, which stopped them ever following a rename: the feature
            // did not work and looked like it did.
            //
            // Not notifying removes the question instead of answering it. A notification now means the
            // user typed, always, so m_settings is updated here by hand for the fields written here.
            if (!m_trackTitleEdited)
            {
                m_trackTitleEditor.setText(mixName, juce::dontSendNotification);
                m_settings.title = mixName.toStdString();
            }

            if (!m_trackNumberEdited)
            {
                const auto trackNumber = leadingTrackNumber(mixName);
                m_trackNumberEditor.setText(trackNumber, juce::dontSendNotification);
                m_settings.trackNumber = trackNumber.toStdString();
            }

            if (!m_outputFileEdited && m_filenameComponent != nullptr)
            {
                // Renaming the mix and then exporting it under the old filename is the half-done rename
                // this feature exists to avoid - it is why one renames before exporting at all. An
                // empty answer means the name cannot be made into a filename that stays in the chosen
                // folder, and then the file the user already has is better than anything this could
                // invent.
                if (const auto renamed = exportFileForName(m_filenameComponent->getCurrentFile(), mixName); renamed != juce::File{})
                {
                    m_filenameComponent->setCurrentFile(renamed, false, juce::dontSendNotification);
                }
            }

            // The caption says which mix this is; it should not still say the old one.
            m_titleLabel.setText(std::format("Export Mix: {}", mixName.toStdString()), juce::dontSendNotification);
        }

        bool ExportMixDialog::commitMixNameIfChanged(std::string &errorOut)
        {
            errorOut.clear();

            // Two comparisons, and they catch different things.
            //
            // The raw one first. A mix stored as " Mix " differs from its own trimmed form, so
            // normalising before comparing would rename it to something nobody typed just because it
            // was exported. Asking whether the editor still holds exactly what was stored answers that
            // whether the user never touched the field or typed in it and put it back - a flag
            // recording that they had once touched it gets the second case wrong.
            if (m_mixNameEditor.getText().toStdString() == m_mixInfo.name)
            {
                return true;
            }

            // Then the effective one, which catches a change that is only padding: typing " Mix "
            // where "Mix" was stored is not a rename worth making.
            const auto newName = effectiveMixName(m_mixNameEditor.getText());
            if (newName.toStdString() == m_mixInfo.name)
            {
                return true;
            }

            if (newName.isEmpty())
            {
                errorOut = "The mix needs a name.";
                return false;
            }

            if (!database::theTrackLibrary.getMixManager().renameMix(m_mixInfo.mixId, newName.toStdString()))
            {
                // The likeliest reason by far, and the only one worth naming: Mixes.name is
                // UNIQUE COLLATE NOCASE, so another mix already answers to this.
                errorOut = "The mix could not be renamed - another mix may already have that name - so nothing was exported.";
                return false;
            }

            spdlog::info("Renamed mix {} from '{}' to '{}' on export.", m_mixInfo.mixId, m_mixInfo.name, newName.toStdString());
            m_mixInfo.name = newName.toStdString();
            return true;
        }

        void ExportMixDialog::loadDefaultTags()
        {
            // Load defaults from settings
            const auto &exportSettings = config::theSettings.exportSettings;

            m_artistEditor.setText(exportSettings.defaultArtist.get());

            // Album and Genre are auto-populated from export folder (via comboBoxChanged)
            // Only set from settings if no folder is selected
            if (m_albumEditor.getText().isEmpty())
            {
                m_albumEditor.setText(exportSettings.defaultAlbum.get());
            }

            // Title, track number and the caption all come from the mix name, through the same path a
            // rename takes, so a dialog that opens and a dialog that has been renamed in agree about
            // what a name implies. Nothing has been typed yet, so nothing is preserved and the flags
            // are all still false.
            applyMixNameDefaults(juce::String{m_mixInfo.name});

            m_yearEditor.setText(exportSettings.defaultYear.get());

            // Genre is auto-populated from export folder (via comboBoxChanged)
            // Only set from settings if no folder is selected
            if (m_genreEditor.getText().isEmpty())
            {
                m_genreEditor.setText(exportSettings.defaultGenre.get());
            }

            m_commentEditor.setText(exportSettings.defaultComment.get());

            // Update settings
            m_settings.artist = m_artistEditor.getText().toStdString();
            m_settings.album = m_albumEditor.getText().toStdString();
            m_settings.title = m_trackTitleEditor.getText().toStdString();
            m_settings.trackNumber = m_trackNumberEditor.getText().toStdString();
            m_settings.year = m_yearEditor.getText().toStdString();
            m_settings.genre = m_genreEditor.getText().toStdString();
            m_settings.comment = m_commentEditor.getText().toStdString();
        }

        void ExportMixDialog::handleExport()
        {
            // Check export folder selection
            if (m_exportFolderCombo.getSelectedId() == 0)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "No Export Folder Selected",
                    "Please select an export folder or create a new one.");
                return;
            }

            const auto file = m_filenameComponent->getCurrentFile();

            if (file == juce::File{})
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "No File Selected", "Please select an output file.");
                return;
            }

            const bool scheduling = m_scheduleCheckbox.getToggleState();

            // Only warn about existing files for immediate export
            if (!scheduling && file.exists())
            {
                const auto result = juce::AlertWindow::showOkCancelBox(
                    juce::AlertWindow::WarningIcon, "File Exists", "The file already exists. Do you want to overwrite it?", "Overwrite", "Cancel");

                if (!result)
                {
                    return;
                }
            }

            // Before anything is written or scheduled, and after the questions that can still send the
            // user back. A rename on a dialog that then failed validation would be a change they did
            // not get to confirm; a rename that fails stops the export, because exporting under a name
            // the library disagrees with is the confusion this feature exists to remove.
            //
            // Scheduling renames too. The name belongs to the mix rather than to the file, so it should
            // be true in the library now, not whenever the queued export happens to run.
            if (std::string renameError; !commitMixNameIfChanged(renameError))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Mix Not Renamed", renameError);
                return;
            }

            m_settings.outputPath = file.getFullPathName().toStdString();
            m_settings.exportFolder = m_exportFolderCombo.getText().toStdString();

            // Save the selected folder for next time
            config::theSettings.exportSettings.lastUsedExportFolder.set(m_settings.exportFolder);

            if (scheduling)
            {
                spdlog::info("Scheduling mix '{}' for export to: {} (Folder: '{}')",
                    m_mixInfo.name, m_settings.outputPath.string(), m_settings.exportFolder);
                closeDialog(Result::ScheduleForLater);
            }
            else
            {
                spdlog::info("Exporting mix '{}' to: {} (Folder: '{}')",
                    m_mixInfo.name, m_settings.outputPath.string(), m_settings.exportFolder);
                if (file.hasFileExtension(".mp3"))
                {
                    spdlog::info("ID3 tags - Artist: '{}', Album: '{}', Title: '{}', Track: '{}', Year: '{}', Genre: '{}', Comment: '{}'",
                        m_settings.artist,
                        m_settings.album,
                        m_settings.title,
                        m_settings.trackNumber,
                        m_settings.year,
                        m_settings.genre,
                        m_settings.comment);
                }
                closeDialog(Result::ExportNow);
            }
        }

        void ExportMixDialog::handleCancel()
        {
            spdlog::debug("Export cancelled by user");
            closeDialog(Result::Cancelled);
        }

        void ExportMixDialog::closeDialog(Result result)
        {
            if (m_callback)
            {
                // m_mixInfo carries the new name when commitMixNameIfChanged accepted one, and the
                // old one otherwise. The caller needs it either way: it runs the export, it logs, and
                // it owns the navigation node that would otherwise keep showing the old name.
                m_callback(result, m_mixInfo, m_settings);
                m_callback = nullptr; // Clear callback after use
            }

            if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
            {
                dw->exitModalState(result != Result::Cancelled ? 1 : 0);
            }
        }

    } // namespace ui
} // namespace jucyaudio