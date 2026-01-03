# VST3 Integration Plan for JucyAudio

## Goal
Enable JucyAudio to host **VST3 audio plugins**, allowing users to apply professional effects (EQ, Compression, Reverb) to individual tracks or the master bus.

## 1. Legal & Licensing
*   **SDK**: Steinberg VST3 SDK (v3.7+).
*   **License**: MIT License (Compatible with JucyAudio's GPLv3).
*   **Constraint**: We will support **VST3 only**. VST2 is legally deprecated and not recommended for new open-source projects.

## 2. Architecture Overview

The integration requires three new subsystems:
1.  **Plugin Management**: Scanning, identifying, and cataloging installed plugins.
2.  **Audio Engine Integration**: Hosting plugin instances and processing audio in the real-time thread.
3.  **UI Integration**: displaying generic or custom plugin editors.

### 2.1 Plugin Management (The "Scanner")
Scanning plugins is slow and crash-prone. It must be isolated.

*   **Class**: `PluginManagerService`
*   **Responsibilities**:
    *   Maintain `juce::KnownPluginList` (persisted to XML/JSON).
    *   Run `juce::PluginDirectoryScanner` in a background thread.
    *   Handle "Blocklisting" of crashing plugins.
*   **Storage**: `Config/plugins.xml` (or similar) to cache scanned plugins so startup remains fast.

### 2.2 Audio Engine Extensions (`MixPlaybackEngine`)

We need to inject effects at two points:
1.  **Per-Track**: Inside `PlaybackTrackSource`.
2.  **Master Bus**: At the end of `MixPlaybackEngine::getNextAudioBlock`.

#### Data Structure Updates
*   **`PlaybackTrackSource`**: Needs a `std::unique_ptr<PluginChain>` to hold effects for that specific track.
*   **`MixPlaybackEngine`**: Needs a `std::unique_ptr<PluginChain>` for the master output.

#### Latency Compensation (PDC)
This is the hardest part.
*   Plugins report latency via `getLatencySamples()`.
*   **Solution**: We must calculate the maximum latency of any path and delay all *other* paths so they align.
*   **Implementation**: Add a `juce::DelayLine` or simple ring buffer to `PlaybackTrackSource` to offset "fast" tracks to match "slow" (heavy plugin) tracks.

### 2.3 UI Components
*   **`PluginScanDialog`**: Shows progress bars while scanning (Files scanned / Found / Failed).
*   **`PluginSelectorMenu`**: A categorized popup menu (Delay, Reverb, EQ) to add effects.
*   **`PluginWindow`**: A desktop window (`juce::DocumentWindow`) that wraps the plugin's native editor (`AudioProcessorEditor`).

## 3. Step-by-Step Implementation Plan

### Phase 1: Infrastructure & Scanning
1.  [ ] **Dependency**: Enable `JUCE_VST3_CAN_REPLACE_VST2=0` and generic VST3 flags in CMake.
2.  [ ] **Manager**: Implement `PluginManagerService` using `juce::AudioPluginFormatManager`.
3.  [ ] **Scanner UI**: Create a `DatabaseMaintenanceDialog` tab or separate dialog to trigger scans.
4.  [ ] **Persistence**: Ensure plugin lists save/load correctly between restarts.

### Phase 2: The `PluginChain` Wrapper
1.  [ ] Create `Audio/Plugins/PluginChain.h`.
    *   Holds `std::vector<std::unique_ptr<juce::AudioPluginInstance>>`.
    *   Has a `processBlock(juce::AudioBuffer&)` method that iterates through plugins.
    *   Handles "Bypassing" logic.
2.  [ ] Create `Audio/Plugins/PluginGraph.h` (Optional, if we want complex routing later).

### Phase 3: Engine Integration
1.  [ ] Modify `PlaybackTrackSource` to own a `PluginChain`.
2.  [ ] Update `MixPlaybackEngine::mixActiveTracksForBlock`:
    *   Process the track's audio through its `PluginChain` *before* adding it to the main mix buffer.
3.  [ ] Add Master Bus processing in `getNextAudioBlock`.

### Phase 4: Latency Compensation (PDC)
1.  [ ] Implement `LatencyCalculator` to sum up latencies in a chain.
2.  [ ] Update `PlaybackTrackSource` to check `MixPlaybackEngine::getMaxLatency()` and delay itself accordingly.

### Phase 5: UI & Editor
1.  [ ] Implement "Add Effect" button in `MixTrackComponent`.
2.  [ ] Handle opening the plugin editor window on click.
3.  [ ] Save/Load plugin state (Project files need to store the plugin's `getStateInformation` binary blob).

## 4. Risks & Mitigations
*   **Stability**: Bad plugins crash the host.
    *   *Mitigation*: We are running plugins in-process (standard for hosts). If a plugin crashes, JucyAudio crashes. We can advise users to stick to stable plugins.
*   **Performance**: VSTs can be CPU heavy.
    *   *Mitigation*: Add a "CPU Load" meter. Implement "Freeze Track" (render to WAV) in the future.
*   **State Compatibility**: If a user uninstalls a plugin, the project won't load correctly.
    *   *Mitigation*: Implement a "Missing Plugins" warning dialog instead of crashing.

# Codex Comments
- Consider isolating plugin scanning in a helper process to avoid app crashes during scan (JUCE supports this pattern).
- Call out where plugin state blobs are stored and how they are versioned/migrated in project files.
- PDC needs a clear policy for max latency and what happens if plugins report extreme values.
