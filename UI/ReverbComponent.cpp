#include "ReverbComponent.h"
#include <UI/CheckboxLookAndFeel.h>
#include <spdlog/spdlog.h>

namespace jucyaudio
{
    namespace ui
    {
        ReverbComponent::ReverbComponent()
        {
            // Setup preset selector
            m_presetLabel.setText("Preset:", juce::dontSendNotification);
            addAndMakeVisible(m_presetLabel);
            addAndMakeVisible(m_presetSelector);
            m_presetSelector.onChange = [this]()
            {
                const auto presetId = getSelectedPresetId();
                if (presetId > 0 && onPresetSelected)
                {
                    m_isUpdatingFromPreset = true;
                    onPresetSelected(presetId);
                    m_isUpdatingFromPreset = false;
                }
            };

            // Setup bypass button
            m_bypassButton.setButtonText("Bypass Reverb");
            addAndMakeVisible(m_bypassButton);
            m_bypassButton.setLookAndFeel(CheckboxLookAndFeel::getInstance());

            m_bypassButton.onClick = [this]()
            {
                notifySettingsChanged();
            };

            // Setup reverb parameter sliders
            setupSlider(m_roomSizeSlider, m_roomSizeLabel, "Room Size", "");
            m_roomSizeSlider.setRange(0.0, 1.0, 0.01);
            m_roomSizeSlider.setValue(0.5);

            setupSlider(m_dampingSlider, m_dampingLabel, "Damping", "");
            m_dampingSlider.setRange(0.0, 1.0, 0.01);
            m_dampingSlider.setValue(0.5);

            setupSlider(m_wetLevelSlider, m_wetLevelLabel, "Wet Mix", "%");
            m_wetLevelSlider.setRange(0.0, 1.0, 0.01);
            m_wetLevelSlider.setValue(0.33);
            m_wetLevelSlider.setTextValueSuffix("%");
            // Convert to percentage for display
            m_wetLevelSlider.textFromValueFunction = [](double value)
            {
                return juce::String(value * 100.0, 0) + "%";
            };
            m_wetLevelSlider.valueFromTextFunction = [](const juce::String &text)
            {
                return text.getDoubleValue() / 100.0;
            };

            setupSlider(m_dryLevelSlider, m_dryLevelLabel, "Dry Mix", "%");
            m_dryLevelSlider.setRange(0.0, 1.0, 0.01);
            m_dryLevelSlider.setValue(0.4);
            m_dryLevelSlider.setTextValueSuffix("%");
            // Convert to percentage for display
            m_dryLevelSlider.textFromValueFunction = [](double value)
            {
                return juce::String(value * 100.0, 0) + "%";
            };
            m_dryLevelSlider.valueFromTextFunction = [](const juce::String &text)
            {
                return text.getDoubleValue() / 100.0;
            };

            setupSlider(m_widthSlider, m_widthLabel, "Width", "");
            m_widthSlider.setRange(0.0, 1.0, 0.01);
            m_widthSlider.setValue(1.0);

            // Setup freeze button
            m_freezeButton.setButtonText("Freeze");
            m_freezeButton.setTooltip("Infinite sustain - holds the current reverb tail");
            addAndMakeVisible(m_freezeButton);
            m_freezeButton.setLookAndFeel(CheckboxLookAndFeel::getInstance());

            m_freezeButton.onClick = [this]()
            {
                notifySettingsChanged();
            };

            // Setup preset management buttons
            m_savePresetButton.setButtonText("Save Preset");
            addAndMakeVisible(m_savePresetButton);
            m_savePresetButton.onClick = [this]()
            {
                showSavePresetDialog();
            };

            m_deletePresetButton.setButtonText("Delete Preset");
            addAndMakeVisible(m_deletePresetButton);
            m_deletePresetButton.onClick = [this]()
            {
                showDeleteConfirmation();
            };

            // Setup reset button
            m_resetButton.setButtonText("Reset All");
            addAndMakeVisible(m_resetButton);
            m_resetButton.onClick = [this]()
            {
                // Reset all parameters to default
                m_roomSizeSlider.setValue(0.5, juce::sendNotification);
                m_dampingSlider.setValue(0.5, juce::sendNotification);
                m_wetLevelSlider.setValue(0.33, juce::sendNotification);
                m_dryLevelSlider.setValue(0.4, juce::sendNotification);
                m_widthSlider.setValue(1.0, juce::sendNotification);
                m_freezeButton.setToggleState(false, juce::sendNotification);
                // Clear preset selection
                m_presetSelector.setSelectedId(0, juce::dontSendNotification);
            };

            setSize(800, 500);
        }

        ReverbComponent::~ReverbComponent() = default;

        void ReverbComponent::setupSlider(juce::Slider &slider, juce::Label &label, const juce::String &labelText, const juce::String &suffix)
        {
            label.setText(labelText, juce::dontSendNotification);
            label.attachToComponent(&slider, false);
            addAndMakeVisible(label);

            slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
            slider.setDoubleClickReturnValue(true, 0.5);
            if (suffix.isNotEmpty())
            {
                slider.setTextValueSuffix(suffix);
            }
            addAndMakeVisible(slider);

            slider.onValueChange = [this]()
            {
                notifySettingsChanged();
            };
        }

        void ReverbComponent::loadPresets(const std::vector<database::model::ReverbPreset> &presets)
        {
            m_presets = presets;
            m_presetSelector.clear();
            m_presetSelector.addItem("-- Select Preset --", 1);

            for (const auto &preset : presets)
            {
                // Use negative IDs for factory presets in the combo box to distinguish them
                const auto comboId = preset.isDeletable ? static_cast<int>(preset.presetId + 1000) : static_cast<int>(preset.presetId + 2);
                m_presetSelector.addItem(preset.name, comboId);
            }
        }

        void ReverbComponent::loadSettings(const audio::model::ReverbSettings &settings)
        {
            m_isUpdatingFromPreset = true;

            // Update bypass button
            m_bypassButton.setToggleState(!settings.isActive, juce::dontSendNotification);

            // Update sliders
            m_roomSizeSlider.setValue(settings.roomSize, juce::dontSendNotification);
            m_dampingSlider.setValue(settings.damping, juce::dontSendNotification);
            m_wetLevelSlider.setValue(settings.wetLevel, juce::dontSendNotification);
            m_dryLevelSlider.setValue(settings.dryLevel, juce::dontSendNotification);
            m_widthSlider.setValue(settings.width, juce::dontSendNotification);

            // Update freeze button
            m_freezeButton.setToggleState(settings.freezeMode > 0.5f, juce::dontSendNotification);

            m_isUpdatingFromPreset = false;
        }

        audio::model::ReverbSettings ReverbComponent::getCurrentSettings() const
        {
            audio::model::ReverbSettings settings;

            settings.isActive = !m_bypassButton.getToggleState();
            settings.roomSize = static_cast<float>(m_roomSizeSlider.getValue());
            settings.damping = static_cast<float>(m_dampingSlider.getValue());
            settings.wetLevel = static_cast<float>(m_wetLevelSlider.getValue());
            settings.dryLevel = static_cast<float>(m_dryLevelSlider.getValue());
            settings.width = static_cast<float>(m_widthSlider.getValue());
            settings.freezeMode = m_freezeButton.getToggleState() ? 1.0f : 0.0f;

            return settings;
        }

        void ReverbComponent::notifySettingsChanged()
        {
            if (!m_isUpdatingFromPreset && onSettingsChanged)
            {
                onSettingsChanged(getCurrentSettings());
            }
        }

        void ReverbComponent::paint(juce::Graphics &g)
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

            // Draw title using theme color
            g.setColour(findColour(juce::Label::textColourId));
            g.setFont(20.0f);
            g.drawText("Master Reverb", getLocalBounds().removeFromTop(40), juce::Justification::centred);
        }

        void ReverbComponent::resized()
        {
            auto area = getLocalBounds();
            area.removeFromTop(40); // Title area

            // Top control row
            auto topRow = area.removeFromTop(60);
            topRow.removeFromLeft(20);

            m_presetLabel.setBounds(topRow.removeFromLeft(50));
            m_presetSelector.setBounds(topRow.removeFromLeft(200));
            topRow.removeFromLeft(20);
            m_bypassButton.setBounds(topRow.removeFromLeft(120));
            topRow.removeFromLeft(20);
            m_freezeButton.setBounds(topRow.removeFromLeft(100));

            // Main parameter area
            area.removeFromTop(20);
            auto paramArea = area.removeFromTop(250);

            const auto sliderWidth = 120;
            const auto spacing = (paramArea.getWidth() - (5 * sliderWidth)) / 6;

            paramArea.removeFromLeft(spacing);

            // Room Size
            auto sliderBounds = paramArea.removeFromLeft(sliderWidth);
            m_roomSizeLabel.setBounds(sliderBounds.removeFromTop(20));
            m_roomSizeSlider.setBounds(sliderBounds);
            paramArea.removeFromLeft(spacing);

            // Damping
            sliderBounds = paramArea.removeFromLeft(sliderWidth);
            m_dampingLabel.setBounds(sliderBounds.removeFromTop(20));
            m_dampingSlider.setBounds(sliderBounds);
            paramArea.removeFromLeft(spacing);

            // Wet Level
            sliderBounds = paramArea.removeFromLeft(sliderWidth);
            m_wetLevelLabel.setBounds(sliderBounds.removeFromTop(20));
            m_wetLevelSlider.setBounds(sliderBounds);
            paramArea.removeFromLeft(spacing);

            // Dry Level
            sliderBounds = paramArea.removeFromLeft(sliderWidth);
            m_dryLevelLabel.setBounds(sliderBounds.removeFromTop(20));
            m_dryLevelSlider.setBounds(sliderBounds);
            paramArea.removeFromLeft(spacing);

            // Width
            sliderBounds = paramArea.removeFromLeft(sliderWidth);
            m_widthLabel.setBounds(sliderBounds.removeFromTop(20));
            m_widthSlider.setBounds(sliderBounds);

            // Bottom button row
            auto bottomSection = area.removeFromBottom(60);
            bottomSection.removeFromTop(10);
            auto buttonRow = bottomSection.removeFromTop(30);
            buttonRow.removeFromLeft(20);
            m_savePresetButton.setBounds(buttonRow.removeFromLeft(120));
            buttonRow.removeFromLeft(10);
            m_deletePresetButton.setBounds(buttonRow.removeFromLeft(120));
            buttonRow.removeFromLeft(10);
            m_resetButton.setBounds(buttonRow.removeFromLeft(100));
        }

        void ReverbComponent::showSavePresetDialog()
        {
            // Create a simple dialog component for preset naming
            class PresetNameDialog : public juce::Component
            {
            public:
                PresetNameDialog(juce::LookAndFeel *laf)
                {
                    // Apply the parent's LookAndFeel immediately
                    setLookAndFeel(laf);

                    addAndMakeVisible(m_label);
                    m_label.setText("Preset Name:", juce::dontSendNotification);

                    addAndMakeVisible(m_textEditor);
                    m_textEditor.setSelectAllWhenFocused(true);

                    addAndMakeVisible(m_saveButton);
                    m_saveButton.setButtonText("Save");
                    m_saveButton.onClick = [this]()
                    {
                        closeWithResult(1);
                    };

                    addAndMakeVisible(m_cancelButton);
                    m_cancelButton.setButtonText("Cancel");
                    m_cancelButton.onClick = [this]()
                    {
                        closeWithResult(0);
                    };

                    setSize(300, 120);
                }

                void parentHierarchyChanged() override
                {
                    // Force proper text colors when added to window
                    m_textEditor.setColour(juce::TextEditor::textColourId, findColour(juce::TextEditor::textColourId));
                    m_textEditor.setColour(juce::TextEditor::backgroundColourId, findColour(juce::TextEditor::backgroundColourId));
                    m_textEditor.applyFontToAllText(m_textEditor.getFont());
                }

                void resized() override
                {
                    auto bounds = getLocalBounds().reduced(10);

                    m_label.setBounds(bounds.removeFromTop(25));
                    m_textEditor.setBounds(bounds.removeFromTop(25));
                    bounds.removeFromTop(10);

                    auto buttonArea = bounds.removeFromTop(30);
                    const int buttonWidth = 80;
                    m_cancelButton.setBounds(buttonArea.removeFromRight(buttonWidth));
                    buttonArea.removeFromRight(10);
                    m_saveButton.setBounds(buttonArea.removeFromRight(buttonWidth));
                }

                void paint(juce::Graphics &g) override
                {
                    g.fillAll(findColour(juce::DialogWindow::backgroundColourId));
                }

                juce::String getName() const
                {
                    return m_textEditor.getText();
                }

                std::function<void(int)> onClose;

            private:
                void closeWithResult(int result)
                {
                    if (onClose)
                        onClose(result);
                }

                juce::Label m_label;
                juce::TextEditor m_textEditor;
                juce::TextButton m_saveButton;
                juce::TextButton m_cancelButton;
            };

            auto *dialogComponent = new PresetNameDialog(&getLookAndFeel());

            juce::DialogWindow::LaunchOptions options;
            options.dialogTitle = "Save Reverb Preset";
            options.content.setOwned(dialogComponent);
            options.componentToCentreAround = this;
            options.dialogBackgroundColour = getLookAndFeel().findColour(juce::DialogWindow::backgroundColourId);
            options.useNativeTitleBar = true;
            options.resizable = false;

            auto *dialog = options.launchAsync();

            dialogComponent->onClose = [this, dialog, dialogComponent](int result)
            {
                if (result == 1)
                {
                    const auto name = dialogComponent->getName();
                    if (name.isNotEmpty() && onSavePreset)
                    {
                        onSavePreset(name, getCurrentSettings());
                    }
                }
                dialog->exitModalState(0);
            };
        }

        void ReverbComponent::showDeleteConfirmation()
        {
            const auto presetId = getSelectedPresetId();
            if (presetId <= 0)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "No Preset Selected", "Please select a preset to delete.");
                return;
            }

            // Find the preset
            auto it = std::find_if(m_presets.begin(),
                m_presets.end(),
                [presetId](const auto &p)
                {
                    return p.presetId == presetId;
                });

            if (it == m_presets.end())
            {
                return;
            }

            if (!it->isDeletable)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon, "Cannot Delete Factory Preset", "This is a factory preset and cannot be deleted.");
                return;
            }

            const auto message = "Are you sure you want to delete the preset '" + it->name + "'?";
            juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon,
                "Delete Preset",
                message,
                "Delete",
                "Cancel",
                nullptr,
                juce::ModalCallbackFunction::create(
                    [this, presetId](int result)
                    {
                        if (result == 1 && onDeletePreset)
                        {
                            onDeletePreset(presetId);
                        }
                    }));
        }

        int64_t ReverbComponent::getSelectedPresetId() const
        {
            const auto selectedId = m_presetSelector.getSelectedId();
            if (selectedId <= 1)
                return -1;

            // Decode the combo box ID back to preset ID
            if (selectedId >= 2000)
            {
                // Factory preset (non-deletable)
                return selectedId - 2;
            }
            else if (selectedId >= 1000)
            {
                // User preset (deletable)
                return selectedId - 1000;
            }

            return -1;
        }
    } // namespace ui
} // namespace jucyaudio