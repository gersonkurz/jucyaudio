#include <UI/ExportMixDialog.h>
#include <UI/Settings.h>
#include <UI/ThemeManager.h>
#include <Database/TrackLibrary.h>
#include <Database/Includes/IMixManager.h>
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

            // Set size after all components are created (increased for schedule checkbox)
            setSize(600, 580);

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

            m_trackTitleEditor.setText(m_mixInfo.name); // Use mix name as default title

            // Extract track number from mix name (e.g., "4025 - Automix 2025-10-26" → "4025")
            juce::String trackNumber;
            const juce::String mixName{m_mixInfo.name};
            const auto firstSpace = mixName.indexOfChar(' ');
            if (firstSpace > 0)
            {
                const auto possibleNumber = mixName.substring(0, firstSpace);
                // Check if it's all digits
                if (possibleNumber.containsOnly("0123456789"))
                {
                    trackNumber = possibleNumber;
                }
            }
            m_trackNumberEditor.setText(trackNumber);

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
                m_callback(result, m_settings);
                m_callback = nullptr; // Clear callback after use
            }

            if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
            {
                dw->exitModalState(result != Result::Cancelled ? 1 : 0);
            }
        }

    } // namespace ui
} // namespace jucyaudio