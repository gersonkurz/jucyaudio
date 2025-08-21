#include "EqualizerDialog.h"
#include <Database/Sqlite/SqliteTrackDatabase.h>
#include <spdlog/spdlog.h>

namespace jucyaudio::ui
{
    EqualizerDialog::EqualizerDialog(database::ITrackDatabase* trackDb,
                                   std::function<void(const audio::model::EQSettings&)> onSettingsChanged)
        : SingletonDialog("Equalizer", 
                         juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::DialogWindow::backgroundColourId),
                         true) // Has close button
    {
        // Create the equalizer component
        m_equalizerComponent = std::make_unique<EqualizerComponent>();
        
        // Load presets from database
        if (auto* sqliteDb = dynamic_cast<database::SqliteTrackDatabase*>(trackDb))
        {
            auto presets = sqliteDb->getEQPresetManager().getAllPresets();
            m_equalizerComponent->loadPresets(presets);
            
            // Set up callbacks
            m_equalizerComponent->onSettingsChanged = onSettingsChanged;
            
            m_equalizerComponent->onPresetSelected = [sqliteDb, this](int64_t presetId)
            {
                auto preset = sqliteDb->getEQPresetManager().getPreset(presetId);
                if (preset.has_value())
                {
                    m_equalizerComponent->loadSettings(preset->settings);
                }
            };
            
            m_equalizerComponent->onSavePreset = [sqliteDb, this](const juce::String& name, const audio::model::EQSettings& settings)
            {
                if (sqliteDb->getEQPresetManager().savePreset(name, settings))
                {
                    auto presets = sqliteDb->getEQPresetManager().getAllPresets();
                    m_equalizerComponent->loadPresets(presets);
                    spdlog::info("Created EQ preset: {}", name.toStdString());
                }
            };
            
            m_equalizerComponent->onDeletePreset = [sqliteDb, this](int64_t presetId)
            {
                if (sqliteDb->getEQPresetManager().deletePreset(presetId))
                {
                    auto presets = sqliteDb->getEQPresetManager().getAllPresets();
                    m_equalizerComponent->loadPresets(presets);
                    spdlog::info("Deleted EQ preset {}", presetId);
                }
            };
        }
        
        setContentOwned(m_equalizerComponent.release(), true);
        setResizable(true, false);
        setUsingNativeTitleBar(true);
        centreWithSize(800, 600);
    }
    
    audio::model::EQSettings EqualizerDialog::getCurrentSettings() const
    {
        if (auto* content = getContentComponent())
        {
            if (auto* eqComp = dynamic_cast<EqualizerComponent*>(content))
            {
                return eqComp->getCurrentSettings();
            }
        }
        return {};
    }
    
    void EqualizerDialog::loadSettings(const audio::model::EQSettings& settings)
    {
        if (auto* content = getContentComponent())
        {
            if (auto* eqComp = dynamic_cast<EqualizerComponent*>(content))
            {
                eqComp->loadSettings(settings);
            }
        }
    }
    
    void EqualizerDialog::showEqualizerDialog(juce::Component* centreAroundComponent,
                                             database::ITrackDatabase* trackDb,
                                             std::function<void(const audio::model::EQSettings&)> onSettingsChanged)
    {
        showSingletonDialog(centreAroundComponent, trackDb, onSettingsChanged);
    }
}