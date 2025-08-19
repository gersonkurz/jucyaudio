#include "EqualizerComponent.h"
#include <spdlog/spdlog.h>

namespace jucyaudio::ui
{
    EqualizerComponent::EqualizerComponent()
    {
        // Setup preset controls
        m_presetLabel.setText("Preset:", juce::dontSendNotification);
        m_presetLabel.attachToComponent(&m_presetSelector, true);
        addAndMakeVisible(m_presetSelector);
        
        m_presetSelector.onChange = [this]()
        {
            if (!m_isUpdatingFromPreset && onPresetSelected)
            {
                const auto presetId = getSelectedPresetId();
                if (presetId >= 0)
                {
                    onPresetSelected(presetId);
                }
            }
        };
        
        // Setup bypass button
        m_bypassButton.setButtonText("Bypass EQ");
        addAndMakeVisible(m_bypassButton);
        m_bypassButton.onClick = [this]() { notifySettingsChanged(); };
        
        // Setup preamp slider
        m_preampLabel.setText("Preamp:", juce::dontSendNotification);
        addAndMakeVisible(m_preampLabel);
        
        m_preampSlider.setRange(-12.0, 12.0, 0.1);
        m_preampSlider.setValue(0.0);
        m_preampSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        m_preampSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
        m_preampSlider.setTextValueSuffix(" dB");
        addAndMakeVisible(m_preampSlider);
        m_preampSlider.onValueChange = [this]() { notifySettingsChanged(); };
        
        // Setup all 10 bands
        for (size_t i = 0; i < m_bandControls.size(); ++i)
        {
            setupBandControls(i);
        }
        
        // Setup preset management buttons
        m_savePresetButton.setButtonText("Save Preset");
        addAndMakeVisible(m_savePresetButton);
        m_savePresetButton.onClick = [this]() { showSavePresetDialog(); };
        
        m_deletePresetButton.setButtonText("Delete Preset");
        addAndMakeVisible(m_deletePresetButton);
        m_deletePresetButton.onClick = [this]() { showDeleteConfirmation(); };
        
        // Setup reset button
        m_resetButton.setButtonText("Reset All");
        addAndMakeVisible(m_resetButton);
        m_resetButton.onClick = [this]() {
            // Reset all bands to 0 dB gain
            for (auto& band : m_bandControls)
            {
                band.gainSlider.setValue(0.0, juce::sendNotification);
                band.enableButton.setToggleState(true, juce::sendNotification);
            }
            // Reset preamp to 0
            m_preampSlider.setValue(0.0, juce::sendNotification);
            // Clear preset selection
            m_presetSelector.setSelectedId(0, juce::dontSendNotification);
        };
        
        setSize(800, 600);
    }
    
    EqualizerComponent::~EqualizerComponent() = default;
    
    void EqualizerComponent::setupBandControls(size_t bandIndex)
    {
        auto& band = m_bandControls[bandIndex];
        const auto freq = audio::model::EQSettings::kDefaultFrequencies[bandIndex];
        
        // Format frequency label
        juce::String freqText;
        if (freq >= 1000.0f)
        {
            freqText = juce::String(freq / 1000.0f, 1) + "k";
        }
        else
        {
            freqText = juce::String(static_cast<int>(freq));
        }
        freqText += " Hz";
        
        // Enable button
        band.enableButton.setButtonText(freqText);
        band.enableButton.setToggleState(true, juce::dontSendNotification);
        addAndMakeVisible(band.enableButton);
        band.enableButton.onClick = [this]() { notifySettingsChanged(); };
        
        // Frequency slider (hidden for now - using fixed frequencies)
        band.frequencySlider.setRange(20.0, 20000.0, 1.0);
        band.frequencySlider.setValue(freq);
        band.frequencySlider.setSkewFactorFromMidPoint(1000.0);
        band.frequencySlider.setVisible(false);
        
        // Gain slider
        band.gainSlider.setRange(-12.0, 12.0, 0.1);
        band.gainSlider.setValue(0.0);
        band.gainSlider.setSliderStyle(juce::Slider::LinearVertical);
        band.gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
        band.gainSlider.setTextValueSuffix(" dB");
        band.gainSlider.setDoubleClickReturnValue(true, 0.0);
        addAndMakeVisible(band.gainSlider);
        band.gainSlider.onValueChange = [this]() { notifySettingsChanged(); };
        
        // Q slider
        band.qSlider.setRange(0.1, 10.0, 0.01);
        band.qSlider.setValue(0.707);
        band.qSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        band.qSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
        band.qSlider.setDoubleClickReturnValue(true, 0.707);
        addAndMakeVisible(band.qSlider);
        band.qSlider.onValueChange = [this]() { notifySettingsChanged(); };
        
        // Labels
        band.gainLabel.setText("Gain", juce::dontSendNotification);
        band.gainLabel.setJustificationType(juce::Justification::centred);
        band.gainLabel.setVisible(false); // We'll show frequency in the enable button
        
        band.qLabel.setText("Q", juce::dontSendNotification);
        band.qLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(band.qLabel);
    }
    
    void EqualizerComponent::loadPresets(const std::vector<database::model::EQPreset>& presets)
    {
        m_isUpdatingFromPreset = true;
        
        m_presets = presets;
        m_presetSelector.clear();
        
        m_presetSelector.addItem("-- Select Preset --", -1);
        
        for (const auto& preset : presets)
        {
            m_presetSelector.addItem(preset.name, static_cast<int>(preset.presetId));
        }
        
        m_isUpdatingFromPreset = false;
    }
    
    void EqualizerComponent::loadSettings(const audio::model::EQSettings& settings)
    {
        m_isUpdatingFromPreset = true;
        
        // Update bypass button
        m_bypassButton.setToggleState(!settings.isActive, juce::dontSendNotification);
        
        // Update preamp
        m_preampSlider.setValue(settings.preampGain, juce::dontSendNotification);
        
        // Update all bands
        for (size_t i = 0; i < m_bandControls.size() && i < settings.bands.size(); ++i)
        {
            updateBandFromSettings(i, settings.bands[i]);
        }
        
        m_isUpdatingFromPreset = false;
    }
    
    void EqualizerComponent::updateBandFromSettings(size_t bandIndex, const audio::model::EQBandSettings& bandSettings)
    {
        auto& band = m_bandControls[bandIndex];
        
        band.enableButton.setToggleState(bandSettings.isActive, juce::dontSendNotification);
        band.frequencySlider.setValue(bandSettings.frequency, juce::dontSendNotification);
        band.gainSlider.setValue(bandSettings.gainInDecibels, juce::dontSendNotification);
        band.qSlider.setValue(bandSettings.quality, juce::dontSendNotification);
    }
    
    audio::model::EQSettings EqualizerComponent::getCurrentSettings() const
    {
        audio::model::EQSettings settings;
        
        settings.isActive = !m_bypassButton.getToggleState();
        settings.preampGain = static_cast<float>(m_preampSlider.getValue());
        
        for (size_t i = 0; i < m_bandControls.size() && i < settings.bands.size(); ++i)
        {
            settings.bands[i] = getBandSettings(i);
        }
        
        return settings;
    }
    
    audio::model::EQBandSettings EqualizerComponent::getBandSettings(size_t bandIndex) const
    {
        const auto& band = m_bandControls[bandIndex];
        
        audio::model::EQBandSettings settings;
        settings.isActive = band.enableButton.getToggleState();
        settings.frequency = static_cast<float>(band.frequencySlider.getValue());
        settings.gainInDecibels = static_cast<float>(band.gainSlider.getValue());
        settings.quality = static_cast<float>(band.qSlider.getValue());
        
        return settings;
    }
    
    void EqualizerComponent::notifySettingsChanged()
    {
        if (!m_isUpdatingFromPreset && onSettingsChanged)
        {
            onSettingsChanged(getCurrentSettings());
        }
    }
    
    void EqualizerComponent::showSavePresetDialog()
    {
        juce::AlertWindow dialog("Save EQ Preset", 
                                "Enter a name for the preset:", 
                                juce::AlertWindow::NoIcon);
        
        dialog.addTextEditor("name", "", "Preset Name:");
        dialog.addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog.addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        
        if (dialog.runModalLoop() == 1)
        {
            const auto name = dialog.getTextEditor("name")->getText();
            if (name.isNotEmpty() && onSavePreset)
            {
                onSavePreset(name, getCurrentSettings());
            }
        }
    }
    
    void EqualizerComponent::showDeleteConfirmation()
    {
        const auto presetId = getSelectedPresetId();
        if (presetId < 0)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "No Preset Selected",
                                                   "Please select a preset to delete.");
            return;
        }
        
        // Find the preset to check if it's deletable
        auto it = std::find_if(m_presets.begin(), m_presets.end(),
                              [presetId](const auto& p) { return p.presetId == presetId; });
        
        if (it != m_presets.end())
        {
            if (!it->isDeletable)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                       "Cannot Delete",
                                                       "System presets cannot be deleted.");
                return;
            }
            
            const auto result = juce::AlertWindow::showYesNoCancelBox(
                juce::AlertWindow::QuestionIcon,
                "Delete Preset",
                "Are you sure you want to delete the preset \"" + it->name + "\"?",
                "Delete", "Cancel", "");
            
            if (result == 1 && onDeletePreset)
            {
                onDeletePreset(presetId);
            }
        }
    }
    
    int64_t EqualizerComponent::getSelectedPresetId() const
    {
        const auto selectedId = m_presetSelector.getSelectedId();
        return selectedId > 0 ? static_cast<int64_t>(selectedId) : -1;
    }
    
    void EqualizerComponent::paint(juce::Graphics& g)
    {
        g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        
        // Draw section separators
        g.setColour(juce::Colours::grey);
        
        // Separator after preset section
        g.drawLine(0, 80, static_cast<float>(getWidth()), 80, 1.0f);
        
        // Separator before save/delete buttons
        g.drawLine(0, static_cast<float>(getHeight() - 60), 
                  static_cast<float>(getWidth()), static_cast<float>(getHeight() - 60), 1.0f);
        
        // Draw frequency labels
        g.setColour(juce::Colours::white);
        g.setFont(12.0f);
        
        // Title
        g.setFont(16.0f);
        g.drawText("10-Band Equalizer", 0, 5, getWidth(), 30, juce::Justification::centred);
    }
    
    void EqualizerComponent::resized()
    {
        const auto bounds = getLocalBounds();
        auto area = bounds;
        
        // Top section for preset selector and bypass
        auto topSection = area.removeFromTop(80);
        topSection.removeFromTop(35); // Space for title
        
        auto presetRow = topSection.removeFromTop(30);
        presetRow.removeFromLeft(60); // Label space
        m_presetSelector.setBounds(presetRow.removeFromLeft(200));
        presetRow.removeFromLeft(20);
        m_bypassButton.setBounds(presetRow.removeFromLeft(100));
        presetRow.removeFromLeft(20);
        
        // Preamp control
        auto preampBounds = presetRow.removeFromLeft(80);
        m_preampLabel.setBounds(preampBounds.removeFromTop(15));
        m_preampSlider.setBounds(preampBounds);
        
        // Bottom section for save/delete buttons
        auto bottomSection = area.removeFromBottom(60);
        bottomSection.removeFromTop(10);
        auto buttonRow = bottomSection.removeFromTop(30);
        buttonRow.removeFromLeft(20);
        m_savePresetButton.setBounds(buttonRow.removeFromLeft(120));
        buttonRow.removeFromLeft(10);
        m_deletePresetButton.setBounds(buttonRow.removeFromLeft(120));
        buttonRow.removeFromLeft(10);
        m_resetButton.setBounds(buttonRow.removeFromLeft(100));
        
        // Main area for EQ bands
        area.removeFromTop(10);
        area.removeFromBottom(10);
        area.removeFromLeft(10);
        area.removeFromRight(10);
        
        const auto bandWidth = area.getWidth() / 10;
        
        for (size_t i = 0; i < m_bandControls.size(); ++i)
        {
            auto bandArea = area.removeFromLeft(bandWidth);
            auto& band = m_bandControls[i];
            
            // Enable button at top
            auto enableArea = bandArea.removeFromTop(30);
            enableArea.reduce(2, 2);
            band.enableButton.setBounds(enableArea);
            
            // Main gain slider in the middle
            auto sliderArea = bandArea.removeFromTop(bandArea.getHeight() - 80);
            sliderArea.reduce(10, 10);
            band.gainSlider.setBounds(sliderArea);
            
            // Q control at bottom
            auto qArea = bandArea;
            qArea.removeFromTop(15); // Space for label
            band.qLabel.setBounds(qArea.removeFromTop(15));
            qArea.reduce(5, 0);
            band.qSlider.setBounds(qArea);
        }
    }
    
} // namespace jucyaudio::ui