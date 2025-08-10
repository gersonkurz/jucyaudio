#include <UI/ExportMixDialog.h>
#include <UI/Settings.h>
#include <UI/ThemeManager.h>
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
              m_tagsHeaderLabel{"tagsHeader", "ID3 Tags (MP3 only):"},
              m_artistLabel{"artistLabel", "Artist:"},
              m_albumLabel{"albumLabel", "Album:"},
              m_trackTitleLabel{"trackTitleLabel", "Title:"},
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

            // Buttons
            addAndMakeVisible(m_exportButton);
            addAndMakeVisible(m_cancelButton);
            m_exportButton.addListener(this);
            m_cancelButton.addListener(this);

            // Load default values
            loadDefaultTags();

            // Set initial visibility based on file extension
            updateTagFieldsVisibility();
            
            // Set size after all components are created
            setSize(600, 500);

            // Set initial focus
            juce::MessageManager::callAsync(
                [this]()
                {
                    if (isShowing())
                    {
                        m_filenameComponent->grabKeyboardFocus();
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

            // Left column: Artist, Album, Title
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
            m_yearLabel.setVisible(isMp3);
            m_yearEditor.setVisible(isMp3);
            m_genreLabel.setVisible(isMp3);
            m_genreEditor.setVisible(isMp3);
            m_commentLabel.setVisible(isMp3);
            m_commentEditor.setVisible(isMp3);

            if (!isMp3)
            {
                // Clear tag fields for non-MP3 exports
                m_settings.artist.clear();
                m_settings.album.clear();
                m_settings.title.clear();
                m_settings.year.clear();
                m_settings.genre.clear();
                m_settings.comment.clear();
            }
        }

        void ExportMixDialog::loadDefaultTags()
        {
            // Load defaults from settings
            const auto &exportSettings = config::theSettings.exportSettings;

            m_artistEditor.setText(exportSettings.defaultArtist.get());
            m_albumEditor.setText(exportSettings.defaultAlbum.get());
            m_trackTitleEditor.setText(m_mixInfo.name); // Use mix name as default title
            m_yearEditor.setText(exportSettings.defaultYear.get());
            m_genreEditor.setText(exportSettings.defaultGenre.get());
            m_commentEditor.setText(exportSettings.defaultComment.get());

            // Update settings
            m_settings.artist = m_artistEditor.getText().toStdString();
            m_settings.album = m_albumEditor.getText().toStdString();
            m_settings.title = m_trackTitleEditor.getText().toStdString();
            m_settings.year = m_yearEditor.getText().toStdString();
            m_settings.genre = m_genreEditor.getText().toStdString();
            m_settings.comment = m_commentEditor.getText().toStdString();
        }

        void ExportMixDialog::handleExport()
        {
            const auto file = m_filenameComponent->getCurrentFile();

            if (file == juce::File{})
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "No File Selected", "Please select an output file.");
                return;
            }

            // Check if file exists and ask for confirmation
            if (file.exists())
            {
                const auto result = juce::AlertWindow::showOkCancelBox(
                    juce::AlertWindow::WarningIcon, "File Exists", "The file already exists. Do you want to overwrite it?", "Overwrite", "Cancel");

                if (!result)
                {
                    return;
                }
            }

            m_settings.outputPath = file.getFullPathName().toStdString();

            spdlog::info("Exporting mix '{}' to: {}", m_mixInfo.name, m_settings.outputPath.string());
            if (file.hasFileExtension(".mp3"))
            {
                spdlog::info("ID3 tags - Artist: '{}', Album: '{}', Title: '{}', Year: '{}', Genre: '{}', Comment: '{}'",
                    m_settings.artist,
                    m_settings.album,
                    m_settings.title,
                    m_settings.year,
                    m_settings.genre,
                    m_settings.comment);
            }

            closeDialog(true);
        }

        void ExportMixDialog::handleCancel()
        {
            spdlog::debug("Export cancelled by user");
            closeDialog(false);
        }

        void ExportMixDialog::closeDialog(bool success)
        {
            if (m_callback)
            {
                m_callback(success, m_settings);
                m_callback = nullptr; // Clear callback after use
            }

            if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
            {
                dw->exitModalState(success ? 1 : 0);
            }
        }

    } // namespace ui
} // namespace jucyaudio