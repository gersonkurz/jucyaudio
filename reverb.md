# JucyAudio Reverb Implementation Plan

## Overview
Add a high-quality master reverb effect to JucyAudio using JUCE's DSP reverb processor. The reverb will be applied to the master output after the equalizer, with full preset support and a dedicated UI window.

## Architecture Decision
**Master Reverb** approach chosen for v1:
- Applied to the entire mix output (after EQ)
- Simpler to implement and understand
- Lower CPU usage than per-track reverb
- Can be upgraded to send/return architecture later if needed

## Implementation Steps

### Step 1: Create Data Models & DSP Engine

#### 1.1 Create `Audio/Model/ReverbSettings.h`
```cpp
// Data model for reverb parameters
struct ReverbSettings {
    float roomSize = 0.5f;      // 0.0 to 1.0
    float damping = 0.5f;        // 0.0 to 1.0  
    float wetLevel = 0.33f;      // 0.0 to 1.0
    float dryLevel = 0.4f;       // 0.0 to 1.0
    float width = 1.0f;          // 0.0 to 1.0
    float freezeMode = 0.0f;     // 0.0 or 1.0
    bool isActive = true;        // Bypass flag
    
    // Serialization for database storage
    nlohmann::json toJson() const;
    static ReverbSettings fromJson(const nlohmann::json& j);
};
```

#### 1.2 Create `Database/Includes/ReverbPreset.h`
```cpp
struct ReverbPreset {
    int64_t presetId;
    std::string name;
    ReverbSettings settings;
    bool isDeletable;  // false for factory presets
};
```

#### 1.3 Create `Audio/Reverb.h/cpp`
```cpp
class Reverb {
    juce::dsp::Reverb reverb;
    std::atomic<bool> bypassFlag{false};
    std::atomic<bool> parametersChanged{false};
    
    // Thread-safe parameter updates
    void updateParameters(const ReverbSettings& settings);
    void process(juce::dsp::AudioBlock<float>& block);
    void prepare(const juce::dsp::ProcessSpec& spec);
    void reset();
};
```

### Step 2: Database Integration

#### 2.1 Create database table (schema v18)
```sql
CREATE TABLE IF NOT EXISTS ReverbPresets (
    preset_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    is_deletable INTEGER NOT NULL DEFAULT 1,
    settings_json TEXT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Insert factory presets
INSERT INTO ReverbPresets (name, is_deletable, settings_json) VALUES
    ('Small Room', 0, '{"roomSize":0.2,"damping":0.7,"wetLevel":0.25,"dryLevel":0.75,"width":0.8}'),
    ('Large Hall', 0, '{"roomSize":0.8,"damping":0.5,"wetLevel":0.35,"dryLevel":0.65,"width":1.0}'),
    ('Cathedral', 0, '{"roomSize":0.95,"damping":0.3,"wetLevel":0.4,"dryLevel":0.6,"width":1.0}'),
    ('Plate', 0, '{"roomSize":0.4,"damping":0.9,"wetLevel":0.3,"dryLevel":0.7,"width":1.0}'),
    ('Spring', 0, '{"roomSize":0.3,"damping":0.6,"wetLevel":0.35,"dryLevel":0.65,"width":0.5}'),
    ('Ambient', 0, '{"roomSize":0.85,"damping":0.2,"wetLevel":0.5,"dryLevel":0.5,"width":1.0}'),
    ('Subtle', 0, '{"roomSize":0.15,"damping":0.8,"wetLevel":0.15,"dryLevel":0.85,"width":0.7}');
```

#### 2.2 Create `Database/Includes/IReverbPresetManager.h`
```cpp
class IReverbPresetManager {
    virtual std::vector<ReverbPreset> getAllPresets() = 0;
    virtual std::optional<ReverbPreset> getPreset(int64_t presetId) = 0;
    virtual std::optional<ReverbPreset> savePreset(const std::string& name, const ReverbSettings& settings) = 0;
    virtual bool deletePreset(int64_t presetId) = 0;
    virtual bool updatePreset(int64_t presetId, const ReverbSettings& settings) = 0;
};
```

#### 2.3 Create `Database/Sqlite/SqliteReverbPresetManager.h/cpp`
Implement the interface with SQLite queries similar to EQPresetManager.

### Step 3: Audio Pipeline Integration

#### 3.1 Update `PlaybackController.h/cpp`
```cpp
private:
    audio::Reverb m_masterReverb;  // Add after m_masterEqualizer
    
public:
    void updateMasterReverb(const audio::model::ReverbSettings& settings);
    
// In getNextAudioBlock():
// Process order: Source -> EQ -> Reverb -> Output
if (eqEnabled) m_masterEqualizer.process(block);
if (reverbEnabled) m_masterReverb.process(block);
```

#### 3.2 Update `MixProjectLoader` 
Add reverb settings storage/retrieval for mix projects (similar to EQ settings).

### Step 4: User Interface

#### 4.1 Create `UI/ReverbComponent.h/cpp`
```cpp
class ReverbComponent : public juce::Component {
    // Controls
    juce::ComboBox m_presetSelector;
    juce::Slider m_roomSizeSlider;      // "Room Size"
    juce::Slider m_dampingSlider;       // "Damping"  
    juce::Slider m_wetLevelSlider;      // "Wet Mix"
    juce::Slider m_dryLevelSlider;      // "Dry Mix"
    juce::Slider m_widthSlider;         // "Width"
    juce::ToggleButton m_freezeButton;  // "Freeze"
    juce::ToggleButton m_bypassButton;  // "Bypass"
    
    // Preset management
    juce::TextButton m_savePresetButton;
    juce::TextButton m_deletePresetButton;
    juce::TextButton m_resetButton;     // Reset to default
    
    // Visual feedback
    // Could add a simple level meter or spectrum display
};
```

### Step 5: Integration & Wiring

#### 5.1 Update `MainComponent`
- Add reverb window management (show/hide)
- Add toolbar button for reverb
- Connect callbacks between ReverbComponent and PlaybackController

#### 5.2 Update `DynamicToolbarComponent`
- Add `ShowReverb` to DataAction enum
- Add reverb.svg icon to toolbar

#### 5.3 Update CMakeLists.txt
Add new source files to the build.

### Step 6: Testing & Polish

#### 6.1 Performance Testing
- Verify CPU usage is acceptable
- Test with various buffer sizes
- Ensure no audio dropouts

#### 6.2 Preset Validation
- Test all factory presets sound good
- Verify save/load/delete operations
- Test parameter automation

#### 6.3 UI Polish
- Add parameter value displays
- Implement double-click to reset
- Add tooltips with descriptions
- Consider adding wet/dry mix lock

## Implementation Notes

### Threading Considerations
- Parameter updates must be thread-safe (use atomics)
- Process callback runs on audio thread
- UI updates happen on message thread

### Parameter Ranges
JUCE's Reverb expects normalized parameters (0.0 to 1.0). The UI should display user-friendly values:
- Room Size: "Small" to "Large"
- Damping: "Bright" to "Dark"  
- Wet/Dry: Percentage display
- Width: "Mono" to "Wide"

### Freeze Mode
When freeze is enabled, the reverb holds the current tail indefinitely. Useful for:
- Creating ambient beds
- Transition effects
- Creative sound design

### Future Enhancements
1. **Send/Return Architecture**: Allow individual track send levels
2. **Multiple Reverb Instances**: Different reverbs for different purposes
3. **Ducking**: Reduce reverb when main signal is loud
4. **Pre-delay**: Add timing offset before reverb
5. **EQ in Reverb**: High/low pass filters on reverb signal
6. **Modulation**: Add chorus/movement to reverb tail
7. **Visual Feedback**: Spectrum analyzer or reverb tail visualization

## File Structure
```
Audio/
  Model/
    ReverbSettings.h
    ReverbSettings.cpp
  Reverb.h
  Reverb.cpp

Database/
  Includes/
    ReverbPreset.h
    IReverbPresetManager.h
  Sqlite/
    SqliteReverbPresetManager.h
    SqliteReverbPresetManager.cpp

UI/
  ReverbComponent.h
  ReverbComponent.cpp
```

## Testing Checklist
- [ ] Reverb processes audio correctly
- [ ] Bypass completely bypasses processing
- [ ] All parameters affect sound appropriately
- [ ] Presets save and load correctly
- [ ] Factory presets cannot be deleted
- [ ] Window opens/closes properly
- [ ] Reset button works
- [ ] No memory leaks
- [ ] Thread-safe parameter updates
- [ ] Acceptable CPU usage
- [ ] No audio artifacts or clicks
- [ ] Settings persist across sessions