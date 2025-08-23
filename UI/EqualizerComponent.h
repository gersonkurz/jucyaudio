#pragma once

#include <Audio/Model/EQSettings.h>
#include <Database/Includes/EQPreset.h>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace jucyaudio
{
    namespace ui
    {
        class EqualizerComponent : public juce::Component
        {
        public:
            EqualizerComponent();
            ~EqualizerComponent() override;

            // Load the list of available presets into the combo box
            void loadPresets(const std::vector<database::model::EQPreset> &presets);

            // Load current settings into all the UI controls
            void loadSettings(const audio::model::EQSettings &settings);

            // Get the current settings from the UI
            audio::model::EQSettings getCurrentSettings() const;

            // Callbacks for user interaction
            std::function<void(const audio::model::EQSettings &)> onSettingsChanged;
            std::function<void(int64_t presetId)> onPresetSelected;
            std::function<void(const juce::String &name, const audio::model::EQSettings &)> onSavePreset;
            std::function<void(int64_t presetId)> onDeletePreset;

            // Component overrides
            void paint(juce::Graphics &g) override;
            void resized() override;

            void parentHierarchyChanged() override;

        private:
            // Master controls
            juce::ComboBox m_presetSelector;
            juce::Label m_presetLabel;
            juce::ToggleButton m_bypassButton;
            juce::Slider m_preampSlider;
            juce::Label m_preampLabel;

            // Band controls (10 bands)
            struct BandControls
            {
                juce::ToggleButton enableButton;
                juce::Slider frequencySlider;
                juce::Slider gainSlider;
                juce::Slider qSlider;
                juce::Label freqLabel;
                juce::Label gainLabel;
                juce::Label qLabel;
            };

            std::array<BandControls, 10> m_bandControls;

            // Preset management buttons
            juce::TextButton m_savePresetButton;
            juce::TextButton m_deletePresetButton;
            juce::TextButton m_resetButton;

            // Internal state
            std::vector<database::model::EQPreset> m_presets;
            bool m_isUpdatingFromPreset{false};

            // Helper methods
            void setupBandControls(size_t bandIndex);
            void updateBandFromSettings(size_t bandIndex, const audio::model::EQBandSettings &bandSettings);
            audio::model::EQBandSettings getBandSettings(size_t bandIndex) const;
            void notifySettingsChanged();
            void showSavePresetDialog();
            void showDeleteConfirmation();
            int64_t getSelectedPresetId() const;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqualizerComponent)
        };
    } // namespace ui
} // namespace jucyaudio