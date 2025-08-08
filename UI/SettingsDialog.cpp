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
        m_headerLabel.setFont(juce::Font(18.0f, juce::Font::bold));
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
        m_noteLabel.setFont(juce::Font(11.0f));
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
        m_artistEditor.setText(config::theSettings.exportSettings.defaultArtist.get());
        m_albumEditor.setText(config::theSettings.exportSettings.defaultAlbum.get());
        m_yearEditor.setText(config::theSettings.exportSettings.defaultYear.get());
        m_genreEditor.setText(config::theSettings.exportSettings.defaultGenre.get());
        m_commentEditor.setText(config::theSettings.exportSettings.defaultComment.get());
        
        spdlog::info("Export settings loaded");
    }
    
    // ===========================================================================
    // GeneralSettingsTab Implementation
    // ===========================================================================
    
    GeneralSettingsTab::GeneralSettingsTab()
    {
        m_headerLabel.setFont(juce::Font(18.0f, juce::Font::bold));
        m_headerLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(m_headerLabel);
        
        m_placeholderLabel.setJustificationType(juce::Justification::centred);
        m_placeholderLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        addAndMakeVisible(m_placeholderLabel);
    }
    
    void GeneralSettingsTab::resized()
    {
        auto bounds = getLocalBounds().reduced(20);
        
        m_headerLabel.setBounds(bounds.removeFromTop(30));
        bounds.removeFromTop(20);
        
        m_placeholderLabel.setBounds(bounds.reduced(20));
    }
    
    void GeneralSettingsTab::saveSettings()
    {
        // No settings to save yet
    }
    
    void GeneralSettingsTab::loadSettings()
    {
        // No settings to load yet
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
        
        setSize(600, 400);
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
        : DialogWindow("Settings", 
                      juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::DialogWindow::backgroundColourId),
                      true) // Has close button
    {
        setContentOwned(new SettingsComponent(), true);
        setResizable(false, false);
        setUsingNativeTitleBar(true);
        
        // Center on screen
        centreWithSize(getWidth(), getHeight());
    }
    
    SettingsDialog::~SettingsDialog()
    {
    }
    
    void SettingsDialog::closeButtonPressed()
    {
        setVisible(false);
    }
    
    void SettingsDialog::showSettingsDialog(juce::Component* centreAroundComponent)
    {
        auto* dialog = new SettingsDialog();
        
        if (centreAroundComponent)
        {
            dialog->centreAroundComponent(centreAroundComponent, dialog->getWidth(), dialog->getHeight());
        }
        
        dialog->setVisible(true);
        dialog->runModalLoop();
        delete dialog;
    }
}