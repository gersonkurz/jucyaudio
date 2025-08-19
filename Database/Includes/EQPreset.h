#pragma once

#include <Audio/Model/EQSettings.h>
#include <juce_core/juce_core.h>
#include <cstdint>

namespace jucyaudio::database::model
{
    struct EQPreset
    {
        int64_t presetId{0};
        juce::String name;
        bool isDeletable{true};  // System presets cannot be deleted
        audio::model::EQSettings settings;
        
        // Factory presets
        static EQPreset createFlatPreset()
        {
            EQPreset preset;
            preset.name = "Flat";
            preset.isDeletable = false;
            preset.settings.resetToFlat();
            return preset;
        }
        
        static EQPreset createRockPreset()
        {
            EQPreset preset;
            preset.name = "Rock";
            preset.isDeletable = false;
            // Classic rock curve: boost bass and treble, slight mid scoop
            preset.settings.bands[0].gainInDecibels = 5.0f;   // 31 Hz
            preset.settings.bands[1].gainInDecibels = 4.0f;   // 63 Hz
            preset.settings.bands[2].gainInDecibels = 3.0f;   // 125 Hz
            preset.settings.bands[3].gainInDecibels = 1.0f;   // 250 Hz
            preset.settings.bands[4].gainInDecibels = -1.0f;  // 500 Hz
            preset.settings.bands[5].gainInDecibels = -1.0f;  // 1 kHz
            preset.settings.bands[6].gainInDecibels = 1.0f;   // 2 kHz
            preset.settings.bands[7].gainInDecibels = 3.0f;   // 4 kHz
            preset.settings.bands[8].gainInDecibels = 4.0f;   // 8 kHz
            preset.settings.bands[9].gainInDecibels = 5.0f;   // 16 kHz
            return preset;
        }
        
        static EQPreset createDancePreset()
        {
            EQPreset preset;
            preset.name = "Dance";
            preset.isDeletable = false;
            // Enhanced bass and presence for dance music
            preset.settings.bands[0].gainInDecibels = 6.0f;   // 31 Hz
            preset.settings.bands[1].gainInDecibels = 5.0f;   // 63 Hz
            preset.settings.bands[2].gainInDecibels = 3.0f;   // 125 Hz
            preset.settings.bands[3].gainInDecibels = 1.0f;   // 250 Hz
            preset.settings.bands[4].gainInDecibels = 0.0f;   // 500 Hz
            preset.settings.bands[5].gainInDecibels = -2.0f;  // 1 kHz
            preset.settings.bands[6].gainInDecibels = -1.0f;  // 2 kHz
            preset.settings.bands[7].gainInDecibels = 0.0f;   // 4 kHz
            preset.settings.bands[8].gainInDecibels = 2.0f;   // 8 kHz
            preset.settings.bands[9].gainInDecibels = 3.0f;   // 16 kHz
            return preset;
        }
        
        static EQPreset createVocalBoostPreset()
        {
            EQPreset preset;
            preset.name = "Vocal Boost";
            preset.isDeletable = false;
            // Enhance vocal presence
            preset.settings.bands[0].gainInDecibels = -2.0f;  // 31 Hz
            preset.settings.bands[1].gainInDecibels = -1.0f;  // 63 Hz
            preset.settings.bands[2].gainInDecibels = 0.0f;   // 125 Hz
            preset.settings.bands[3].gainInDecibels = 1.0f;   // 250 Hz
            preset.settings.bands[4].gainInDecibels = 2.0f;   // 500 Hz
            preset.settings.bands[5].gainInDecibels = 3.0f;   // 1 kHz
            preset.settings.bands[6].gainInDecibels = 4.0f;   // 2 kHz
            preset.settings.bands[7].gainInDecibels = 3.0f;   // 4 kHz
            preset.settings.bands[8].gainInDecibels = 2.0f;   // 8 kHz
            preset.settings.bands[9].gainInDecibels = 1.0f;   // 16 kHz
            return preset;
        }
        
        static EQPreset createBassBoostPreset()
        {
            EQPreset preset;
            preset.name = "Bass Boost";
            preset.isDeletable = false;
            // Heavy bass emphasis
            preset.settings.bands[0].gainInDecibels = 8.0f;   // 31 Hz
            preset.settings.bands[1].gainInDecibels = 7.0f;   // 63 Hz
            preset.settings.bands[2].gainInDecibels = 5.0f;   // 125 Hz
            preset.settings.bands[3].gainInDecibels = 3.0f;   // 250 Hz
            preset.settings.bands[4].gainInDecibels = 1.0f;   // 500 Hz
            preset.settings.bands[5].gainInDecibels = 0.0f;   // 1 kHz
            preset.settings.bands[6].gainInDecibels = 0.0f;   // 2 kHz
            preset.settings.bands[7].gainInDecibels = 0.0f;   // 4 kHz
            preset.settings.bands[8].gainInDecibels = 0.0f;   // 8 kHz
            preset.settings.bands[9].gainInDecibels = 0.0f;   // 16 kHz
            return preset;
        }
        
        static EQPreset createTrebleBoostPreset()
        {
            EQPreset preset;
            preset.name = "Treble Boost";
            preset.isDeletable = false;
            // Enhanced high frequencies
            preset.settings.bands[0].gainInDecibels = 0.0f;   // 31 Hz
            preset.settings.bands[1].gainInDecibels = 0.0f;   // 63 Hz
            preset.settings.bands[2].gainInDecibels = 0.0f;   // 125 Hz
            preset.settings.bands[3].gainInDecibels = 0.0f;   // 250 Hz
            preset.settings.bands[4].gainInDecibels = 0.0f;   // 500 Hz
            preset.settings.bands[5].gainInDecibels = 1.0f;   // 1 kHz
            preset.settings.bands[6].gainInDecibels = 3.0f;   // 2 kHz
            preset.settings.bands[7].gainInDecibels = 5.0f;   // 4 kHz
            preset.settings.bands[8].gainInDecibels = 7.0f;   // 8 kHz
            preset.settings.bands[9].gainInDecibels = 8.0f;   // 16 kHz
            return preset;
        }
    };
    
} // namespace jucyaudio::database::model