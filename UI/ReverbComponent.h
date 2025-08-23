#pragma once

#include <Audio/Model/ReverbSettings.h>
#include <Database/Includes/ReverbPreset.h>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace jucyaudio
{
    namespace ui
    {
        class ReverbComponent : public juce::Component
        {
        public:
            ReverbComponent();
            ~ReverbComponent() override;

            // Load the list of available presets into the combo box
            void loadPresets(const std::vector<database::model::ReverbPreset> &presets);

            // Load current settings into all the UI controls
            void loadSettings(const audio::model::ReverbSettings &settings);

            // Get the current settings from the UI
            audio::model::ReverbSettings getCurrentSettings() const;

            // Callbacks for user interaction
            std::function<void(const audio::model::ReverbSettings &)> onSettingsChanged;
            std::function<void(int64_t presetId)> onPresetSelected;
            std::function<void(const juce::String &name, const audio::model::ReverbSettings &)> onSavePreset;
            std::function<void(int64_t presetId)> onDeletePreset;

            // Component overrides
            void paint(juce::Graphics &g) override;
            void resized() override;

        private:
            // Master controls
            juce::ComboBox m_presetSelector;
            juce::Label m_presetLabel;
            juce::ToggleButton m_bypassButton;

            // Reverb parameter sliders
            juce::Slider m_roomSizeSlider;
            juce::Label m_roomSizeLabel;

            juce::Slider m_dampingSlider;
            juce::Label m_dampingLabel;

            juce::Slider m_wetLevelSlider;
            juce::Label m_wetLevelLabel;

            juce::Slider m_dryLevelSlider;
            juce::Label m_dryLevelLabel;

            juce::Slider m_widthSlider;
            juce::Label m_widthLabel;

            // Freeze mode button
            juce::ToggleButton m_freezeButton;

            // Preset management buttons
            juce::TextButton m_savePresetButton;
            juce::TextButton m_deletePresetButton;
            juce::TextButton m_resetButton;

            // Internal state
            std::vector<database::model::ReverbPreset> m_presets;
            bool m_isUpdatingFromPreset{false};

            // Helper methods
            void setupSlider(juce::Slider &slider, juce::Label &label, const juce::String &labelText, const juce::String &suffix = "");
            void notifySettingsChanged();
            void showSavePresetDialog();
            void showDeleteConfirmation();
            int64_t getSelectedPresetId() const;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbComponent)
        };
    } // namespace ui
} // namespace jucyaudio