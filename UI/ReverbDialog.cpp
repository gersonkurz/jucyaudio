#include "ReverbDialog.h"
#include <Database/Sqlite/SqliteTrackDatabase.h>
#include <spdlog/spdlog.h>

namespace jucyaudio::ui
{
    ReverbDialog::ReverbDialog(database::ITrackDatabase* trackDb,
                             std::function<void(const audio::model::ReverbSettings&)> onSettingsChanged,
                             const audio::model::ReverbSettings& initialSettings)
        : SingletonDialog("Reverb", 
                         juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::DialogWindow::backgroundColourId),
                         true), // Has close button
          m_onSettingsChanged(onSettingsChanged)
    {
        // Create the reverb component
        m_reverbComponent = std::make_unique<ReverbComponent>();
        
        // Load presets from database
        if (auto* sqliteDb = dynamic_cast<database::SqliteTrackDatabase*>(trackDb))
        {
            auto presets = sqliteDb->getReverbPresetManager().getAllPresets();
            m_reverbComponent->loadPresets(presets);
            
            // Set up callbacks
            m_reverbComponent->onSettingsChanged = onSettingsChanged;
            
            m_reverbComponent->onPresetSelected = [sqliteDb, this](int64_t presetId)
            {
                auto preset = sqliteDb->getReverbPresetManager().getPreset(presetId);
                if (preset.has_value())
                {
                    m_reverbComponent->loadSettings(preset->settings);
                }
            };
            
            m_reverbComponent->onSavePreset = [sqliteDb, this](const juce::String& name, const audio::model::ReverbSettings& settings)
            {
                if (sqliteDb->getReverbPresetManager().savePreset(name.toStdString(), settings))
                {
                    auto presets = sqliteDb->getReverbPresetManager().getAllPresets();
                    m_reverbComponent->loadPresets(presets);
                    spdlog::info("Created reverb preset: {}", name.toStdString());
                }
            };
            
            m_reverbComponent->onDeletePreset = [sqliteDb, this](int64_t presetId)
            {
                if (sqliteDb->getReverbPresetManager().deletePreset(presetId))
                {
                    auto presets = sqliteDb->getReverbPresetManager().getAllPresets();
                    m_reverbComponent->loadPresets(presets);
                    spdlog::info("Deleted reverb preset {}", presetId);
                }
            };
        }
        
        // Load initial settings
        if (auto* content = m_reverbComponent.get())
        {
            content->loadSettings(initialSettings);
        }
        
        setContentOwned(m_reverbComponent.release(), true);
        setResizable(true, false);
        setUsingNativeTitleBar(true);
        centreWithSize(800, 500);
    }

    ReverbDialog::~ReverbDialog() = default;
    
    void ReverbDialog::closeButtonPressed()
    {
        // Apply current settings before closing
        if (m_onSettingsChanged)
        {
            m_onSettingsChanged(getCurrentSettings());
        }
        SingletonDialog::closeButtonPressed();
    }
    
    bool ReverbDialog::escapeKeyPressed()
    {
        // Apply current settings before closing
        if (m_onSettingsChanged)
        {
            m_onSettingsChanged(getCurrentSettings());
        }
        return SingletonDialog::escapeKeyPressed();
    }
    
    audio::model::ReverbSettings ReverbDialog::getCurrentSettings() const
    {
        if (auto* content = getContentComponent())
        {
            if (auto* reverbComp = dynamic_cast<ReverbComponent*>(content))
            {
                return reverbComp->getCurrentSettings();
            }
        }
        return {};
    }
    
    void ReverbDialog::loadSettings(const audio::model::ReverbSettings& settings)
    {
        if (auto* content = getContentComponent())
        {
            if (auto* reverbComp = dynamic_cast<ReverbComponent*>(content))
            {
                reverbComp->loadSettings(settings);
            }
        }
    }
    
    void ReverbDialog::showReverbDialog(juce::Component* centreAroundComponent,
                                       database::ITrackDatabase* trackDb,
                                       std::function<void(const audio::model::ReverbSettings&)> onSettingsChanged,
                                       const audio::model::ReverbSettings& initialSettings)
    {
        showSingletonDialog(centreAroundComponent, trackDb, onSettingsChanged, initialSettings);
    }
}