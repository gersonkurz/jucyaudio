#include "SettingsDialog.h"
#include <UI/Settings.h>
#include <Config/toml_backend.h>
#include <spdlog/spdlog.h>
#include <chrono>

namespace jucyaudio::ui
{
    extern std::string g_strConfigFilename;
    // ===========================================================================
    // ExportSettingsTab Implementation
    // ===========================================================================
    
    ExportSettingsTab::ExportSettingsTab()
    {
        // Configure header
        m_headerLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(24.0f)}.boldened());
        m_headerLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(m_headerLabel);
        
        // Configure labels
        for (auto* label : {&m_artistLabel, &m_albumLabel, &m_yearLabel, 
                           &m_genreLabel, &m_commentLabel})
        {
            label->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(label);
        }
        
        // Configure text editors
        for (auto* editor : {&m_artistEditor, &m_albumEditor, &m_yearEditor,
                            &m_genreEditor, &m_commentEditor})
        {
            editor->setMultiLine(false);
            editor->setReturnKeyStartsNewLine(false);
            editor->setScrollbarsShown(false);
            addAndMakeVisible(editor);
        }
        
        // Set default year to current year
        const auto now = std::chrono::system_clock::now();
        const auto time_t = std::chrono::system_clock::to_time_t(now);
        const auto* tm = std::localtime(&time_t);
        m_yearEditor.setText(juce::String(1900 + tm->tm_year));
        
        // Configure note label
        m_noteLabel.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
        m_noteLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        m_noteLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(m_noteLabel);
        
        // Load existing settings
        loadSettings();
    }
    
    void ExportSettingsTab::resized()
    {
        auto bounds = getLocalBounds().reduced(20);
        
        m_headerLabel.setBounds(bounds.removeFromTop(30));
        bounds.removeFromTop(20); // spacing
        
        const int labelWidth = 100;
        const int rowHeight = 30;
        const int spacing = 10;
        
        // Artist row
        auto row = bounds.removeFromTop(rowHeight);
        m_artistLabel.setBounds(row.removeFromLeft(labelWidth));
        row.removeFromLeft(spacing);
        m_artistEditor.setBounds(row);
        bounds.removeFromTop(spacing);
        
        // Album row
        row = bounds.removeFromTop(rowHeight);
        m_albumLabel.setBounds(row.removeFromLeft(labelWidth));
        row.removeFromLeft(spacing);
        m_albumEditor.setBounds(row);
        bounds.removeFromTop(spacing);
        
        // Year row
        row = bounds.removeFromTop(rowHeight);
        m_yearLabel.setBounds(row.removeFromLeft(labelWidth));
        row.removeFromLeft(spacing);
        m_yearEditor.setBounds(row.removeFromLeft(100)); // Year field is smaller
        bounds.removeFromTop(spacing);
        
        // Genre row
        row = bounds.removeFromTop(rowHeight);
        m_genreLabel.setBounds(row.removeFromLeft(labelWidth));
        row.removeFromLeft(spacing);
        m_genreEditor.setBounds(row);
        bounds.removeFromTop(spacing);
        
        // Comment row
        row = bounds.removeFromTop(rowHeight);
        m_commentLabel.setBounds(row.removeFromLeft(labelWidth));
        row.removeFromLeft(spacing);
        m_commentEditor.setBounds(row);
        bounds.removeFromTop(spacing * 2);
        
        // Note at bottom
        m_noteLabel.setBounds(bounds.removeFromTop(40));
    }
    
    void ExportSettingsTab::saveSettings()
    {
        config::theSettings.exportSettings.defaultArtist.set(m_artistEditor.getText().toStdString());
        config::theSettings.exportSettings.defaultAlbum.set(m_albumEditor.getText().toStdString());
        config::theSettings.exportSettings.defaultYear.set(m_yearEditor.getText().toStdString());
        config::theSettings.exportSettings.defaultGenre.set(m_genreEditor.getText().toStdString());
        config::theSettings.exportSettings.defaultComment.set(m_commentEditor.getText().toStdString());
        
        // Save to TOML file
        config::TomlBackend backend{g_strConfigFilename};
        config::theSettings.save(backend);
        
        spdlog::info("Export settings saved");
    }
    
    void ExportSettingsTab::loadSettings()
    {
        m_artistEditor.setText(juce::String{config::theSettings.exportSettings.defaultArtist});
        m_albumEditor.setText(juce::String{config::theSettings.exportSettings.defaultAlbum});
        m_yearEditor.setText(juce::String{config::theSettings.exportSettings.defaultYear});
        m_genreEditor.setText(juce::String{config::theSettings.exportSettings.defaultGenre});
        m_commentEditor.setText(juce::String{config::theSettings.exportSettings.defaultComment});
        
        spdlog::info("Export settings loaded");
    }
    
    void ExportSettingsTab::parentHierarchyChanged()
    {
        // Force text color refresh when the component is added to the window hierarchy
        // This ensures the L&F colors are properly applied to existing text
        for (auto* editor : {&m_artistEditor, &m_albumEditor, &m_yearEditor,
                            &m_genreEditor, &m_commentEditor})
        {
            // Get the current text
            const auto text = editor->getText();
            // Clear and re-set to force color update
            editor->clear();
            editor->setText(text, juce::dontSendNotification);
            // Apply the font to ensure proper formatting
            editor->applyFontToAllText(editor->getFont());
        }
    }
    
    // ===========================================================================
    // GeneralSettingsTab Implementation
    // ===========================================================================
    
    GeneralSettingsTab::GeneralSettingsTab()
    {
        auto setupLabel = [this](juce::Label& label, const juce::String& text, float fontSize, bool isBold)
        {
            juce::Font font{juce::FontOptions{}.withHeight(fontSize)};
            if (isBold)
                font = font.boldened();
            label.setFont(font);
            label.setText(text, juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(label);
        };

        // Backup Settings
        setupLabel(m_backupLabel, "Backup", 18.0f, true);
        setupLabel(m_backupSliderLabel, "Number of backups to keep:", 15.0f, false);
        m_backupSlider.setRange(1, 20, 1);
        m_backupSlider.setSliderStyle(juce::Slider::IncDecButtons);
        m_backupSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 20);
        addAndMakeVisible(m_backupSlider);

        // Mix Editing Settings
        setupLabel(m_mixEditingLabel, "Mix Editing", 18.0f, true);
        m_removeFromWsToggle.setButtonText("Remove tracks from Working Set when deleting from a Mix");
        addAndMakeVisible(m_removeFromWsToggle);
        m_askBeforeRemovingToggle.setButtonText("Show confirmation before removing tracks from a Working Set");
        addAndMakeVisible(m_askBeforeRemovingToggle);
        m_clearWsAfterExportToggle.setButtonText("Clear Working Set after a successful export");
        addAndMakeVisible(m_clearWsAfterExportToggle);
        setupLabel(m_removeTrackOptionLabel, "Default action for removing tracks from Mix:", 15.0f, false);
        m_removeTrackOptionCombo.addItem("Remove from Mix and Working Set", (int)config::RemoveTrackOption::RemoveFromBoth + 1);
        m_removeTrackOptionCombo.addItem("Remove from Mix Only", (int)config::RemoveTrackOption::RemoveFromMixOnly + 1);
        m_removeTrackOptionCombo.addItem("Always Ask", (int)config::RemoveTrackOption::AskUser + 1);
        addAndMakeVisible(m_removeTrackOptionCombo);

        // Logging Settings
        setupLabel(m_loggingLabel, "Logging", 18.0f, true);
        setupLabel(m_logLevelLabel, "Log Level:", 15.0f, false);
        m_logLevelCombo.addItem("Trace", 1);
        m_logLevelCombo.addItem("Debug", 2);
        m_logLevelCombo.addItem("Info", 3);
        m_logLevelCombo.addItem("Warn", 4);
        m_logLevelCombo.addItem("Error", 5);
        m_logLevelCombo.addItem("Critical", 6);
        addAndMakeVisible(m_logLevelCombo);

        loadSettings();
    }
    
    void GeneralSettingsTab::resized()
    {
        auto bounds = getLocalBounds().reduced(20);
        const int rowHeight = 25;
        const int groupSpacing = 20;
        const int labelWidth = 400; // Increased width for labels

        // Backup section
        m_backupLabel.setBounds(bounds.removeFromTop(30));
        auto backupRow = bounds.removeFromTop(rowHeight);
        m_backupSliderLabel.setBounds(backupRow.removeFromLeft(labelWidth));
        m_backupSlider.setBounds(backupRow);
        bounds.removeFromTop(groupSpacing);

        // Mix Editing section
        m_mixEditingLabel.setBounds(bounds.removeFromTop(30));
        m_removeFromWsToggle.setBounds(bounds.removeFromTop(rowHeight));
        m_askBeforeRemovingToggle.setBounds(bounds.removeFromTop(rowHeight));
        m_clearWsAfterExportToggle.setBounds(bounds.removeFromTop(rowHeight));
        auto mixRow = bounds.removeFromTop(rowHeight);
        m_removeTrackOptionLabel.setBounds(mixRow.removeFromLeft(labelWidth));
        m_removeTrackOptionCombo.setBounds(mixRow);
        bounds.removeFromTop(groupSpacing);

        // Logging section
        m_loggingLabel.setBounds(bounds.removeFromTop(30));
        auto logRow = bounds.removeFromTop(rowHeight);
        m_logLevelLabel.setBounds(logRow.removeFromLeft(labelWidth));
        m_logLevelCombo.setBounds(logRow);
    }
    
    void GeneralSettingsTab::saveSettings()
    {
        // Backup
        config::theSettings.backupSettings.numberOfBackups.set((int)m_backupSlider.getValue());

        // Mix Editing
        config::theSettings.mixEditingSettings.removeFromWorkingSetOnDelete.set(m_removeFromWsToggle.getToggleState());
        config::theSettings.mixEditingSettings.askBeforeRemovingFromWorkingSet.set(m_askBeforeRemovingToggle.getToggleState());
        config::theSettings.mixEditingSettings.clearWorkingSetAfterExport.set(m_clearWsAfterExportToggle.getToggleState());
        auto removeOption = (config::RemoveTrackOption)(m_removeTrackOptionCombo.getSelectedId() - 1);
        config::theSettings.mixEditingSettings.removeTrackOption.set(removeOption);

        // Logging
        config::theSettings.loggingSettings.logLevel.set(m_logLevelCombo.getText().toLowerCase().toStdString());

        // TODO: Add call to dynamically update log level here

        // Save all to TOML file
        config::TomlBackend backend{g_strConfigFilename};
        config::theSettings.save(backend);
        
        spdlog::info("General settings saved");
    }
    
    void GeneralSettingsTab::loadSettings()
    {
        // Backup
        m_backupSlider.setValue(config::theSettings.backupSettings.numberOfBackups, juce::dontSendNotification);

        // Mix Editing
        m_removeFromWsToggle.setToggleState(config::theSettings.mixEditingSettings.removeFromWorkingSetOnDelete, juce::dontSendNotification);
        m_askBeforeRemovingToggle.setToggleState(config::theSettings.mixEditingSettings.askBeforeRemovingFromWorkingSet, juce::dontSendNotification);
        m_clearWsAfterExportToggle.setToggleState(config::theSettings.mixEditingSettings.clearWorkingSetAfterExport, juce::dontSendNotification);
        m_removeTrackOptionCombo.setSelectedId((int)config::theSettings.mixEditingSettings.removeTrackOption.get().value + 1, juce::dontSendNotification);

        // Logging
        m_logLevelCombo.setText(juce::String{config::theSettings.loggingSettings.logLevel}, juce::dontSendNotification);
        
        spdlog::info("General settings loaded");
    }

    void GeneralSettingsTab::parentHierarchyChanged()
    {
        // Custom LookAndFeel class to fix checkbox rendering in light theme
        class CheckboxLookAndFeel : public juce::LookAndFeel_V4
        {
        public:
            CheckboxLookAndFeel(juce::LookAndFeel& parent)
            {
                // Copy colors from parent to ensure we match the current theme
                setColour(juce::ToggleButton::textColourId, parent.findColour(juce::ToggleButton::textColourId));
                setColour(juce::ToggleButton::tickColourId, parent.findColour(juce::ToggleButton::textColourId)); // Tick the same color as text
                setColour(juce::ToggleButton::tickDisabledColourId, parent.findColour(juce::ToggleButton::textColourId).withAlpha(0.5f));
            }
            
            void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
            {
                juce::ignoreUnused(shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
                const auto fontSize = juce::jmin(15.0f, button.getHeight() * 0.75f);
                const auto tickWidth = fontSize * 1.1f;
                
                // Draw checkbox outline
                juce::Rectangle<float> tickBounds(4.0f, (button.getHeight() - tickWidth) * 0.5f, tickWidth, tickWidth);
                
                g.setColour(button.findColour(juce::ToggleButton::textColourId).withAlpha(0.8f));
                g.drawRect(tickBounds, 1.0f);
                
                // Fill if checked
                if (button.getToggleState())
                {
                    g.setColour(button.findColour(juce::ToggleButton::tickColourId));
                    const auto tick = tickBounds.reduced(tickWidth * 0.25f);
                    
                    juce::Path p;
                    p.startNewSubPath(tick.getX(), tick.getCentreY());
                    p.lineTo(tick.getCentreX(), tick.getBottom());
                    p.lineTo(tick.getRight(), tick.getY());
                    
                    g.strokePath(p, juce::PathStrokeType(2.0f));
                }
                
                // Draw text
                g.setColour(button.findColour(juce::ToggleButton::textColourId));
                g.setFont(fontSize);
                
                if (!button.isEnabled())
                    g.setOpacity(0.5f);
                
                g.drawFittedText(button.getButtonText(),
                               button.getLocalBounds().withTrimmedLeft(juce::roundToInt(tickWidth) + 10).withTrimmedRight(2),
                               juce::Justification::centredLeft, 10);
            }
        };
        
        // Create and set custom LookAndFeel for checkboxes
        // We create a new instance because it captures the parent L&F colors.
        m_checkboxLookAndFeel = std::make_unique<CheckboxLookAndFeel>(getLookAndFeel());
        m_removeFromWsToggle.setLookAndFeel(m_checkboxLookAndFeel.get());
        m_askBeforeRemovingToggle.setLookAndFeel(m_checkboxLookAndFeel.get());
        m_clearWsAfterExportToggle.setLookAndFeel(m_checkboxLookAndFeel.get());
    }
    
    // ===========================================================================
    // SettingsComponent Implementation
    // ===========================================================================
    
    SettingsDialog::SettingsComponent::SettingsComponent()
    {
        // Create tabs
        m_generalTab = std::make_unique<GeneralSettingsTab>();
        m_exportTab = std::make_unique<ExportSettingsTab>();
        
        // Add tabs to tabbed component
        m_tabbedComponent.addTab("General", 
                                juce::Colours::transparentBlack, 
                                m_generalTab.get(), 
                                false); // Don't delete component
                                
        m_tabbedComponent.addTab("Export Settings", 
                                juce::Colours::transparentBlack, 
                                m_exportTab.get(), 
                                false);
        
        addAndMakeVisible(m_tabbedComponent);
        
        // Configure buttons
        m_saveButton.onClick = [this]()
        {
            saveAllSettings();
            if (auto* dialog = findParentComponentOfClass<SettingsDialog>())
            {
                dialog->closeButtonPressed();
            }
        };
        addAndMakeVisible(m_saveButton);
        
        m_cancelButton.onClick = [this]()
        {
            if (auto* dialog = findParentComponentOfClass<SettingsDialog>())
            {
                dialog->closeButtonPressed();
            }
        };
        addAndMakeVisible(m_cancelButton);
        
        setSize(750, 500); // Increased width and height for new settings
    }
    
    void SettingsDialog::SettingsComponent::resized()
    {
        auto bounds = getLocalBounds();
        
        // Button bar at bottom
        const int buttonBarHeight = 40;
        auto buttonBar = bounds.removeFromBottom(buttonBarHeight);
        buttonBar = buttonBar.reduced(10, 5);
        
        const int buttonWidth = 80;
        const int buttonSpacing = 10;
        
        m_cancelButton.setBounds(buttonBar.removeFromRight(buttonWidth));
        buttonBar.removeFromRight(buttonSpacing);
        m_saveButton.setBounds(buttonBar.removeFromRight(buttonWidth));
        
        // Tabs fill remaining space
        m_tabbedComponent.setBounds(bounds);
    }
    
    void SettingsDialog::SettingsComponent::saveAllSettings()
    {
        m_generalTab->saveSettings();
        m_exportTab->saveSettings();
    }
    
    // ===========================================================================
    // SettingsDialog Implementation
    // ===========================================================================
    
    SettingsDialog::SettingsDialog()
        : SingletonDialog("Settings", 
                         juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::DialogWindow::backgroundColourId),
                         true) // Has close button
    {
        // Don't create content here - it will be done after L&F is set
        setResizable(false, false);
    }
    
    void SettingsDialog::initializeContent()
    {
        // This should be called AFTER the L&F has been set by showSingletonDialog
        if (getContentComponent() == nullptr)
        {
            setContentOwned(new SettingsComponent(), true);
        }
    }
    
    void SettingsDialog::showSettingsDialog(juce::Component* centreAroundComponent)
    {
        // Check if dialog already exists
        static SettingsDialog* existingDialog = nullptr;
        
        if (existingDialog && existingDialog->isVisible())
        {
            existingDialog->toFront(true);
            return;
        }
        
        // Create new dialog
        auto* dialog = new SettingsDialog();
        existingDialog = dialog;
        
        // Apply L&F BEFORE creating content
        if (centreAroundComponent)
        {
            dialog->setLookAndFeel(&centreAroundComponent->getLookAndFeel());
        }
        
        // Now create content with proper L&F
        dialog->initializeContent();
        
        // Configure and show
        dialog->setAlwaysOnTop(true);
        dialog->setUsingNativeTitleBar(true);
        
        if (centreAroundComponent)
        {
            dialog->centreAroundComponent(centreAroundComponent, 
                                         dialog->getWidth(), 
                                         dialog->getHeight());
        }
        else
        {
            dialog->centreWithSize(dialog->getWidth(), dialog->getHeight());
        }
        
        dialog->setVisible(true);
        dialog->toFront(true);
        dialog->grabKeyboardFocus();
    }
}
